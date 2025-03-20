#include "llvm/IR/PassManager.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Constants.h"
#include <llvm/IR/Module.h>
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include "llvm/Analysis/DominanceFrontier.h"
#include "llvm/Analysis/PostDominators.h"

using namespace llvm;

extern "C" {
    void init_shared_mem(int is_creator);  // 参数类型应为int
    void add_controlflow_entry(uint64_t source_id, uint64_t offset);
}

class ControlFlowInstrumentPass : public PassInfoMixin<ControlFlowInstrumentPass> {
private:
    GlobalVariable *BaseAddrGV = nullptr;

public:
    // 优化全局构造函数插入
    void addGlobalInitializer(Module &M) {
        if (M.getFunction("cf_initializer")) return;

        LLVMContext &Ctx = M.getContext();
        IRBuilder<> Builder(Ctx);

         // 添加extern声明
        FunctionType *InitType = FunctionType::get(Builder.getVoidTy(), {Builder.getInt32Ty()}, false);
        Function::Create(InitType, GlobalValue::ExternalLinkage, "init_shared_mem", &M);

        FunctionType *CtorType = FunctionType::get(Builder.getVoidTy(), false);
        Function *Ctor = Function::Create(CtorType, 
            GlobalValue::InternalLinkage, "cf_initializer", &M);
        
        BasicBlock *BB = BasicBlock::Create(Ctx, "entry", Ctor);
        Builder.SetInsertPoint(BB);
        
        FunctionCallee InitFunc = M.getOrInsertFunction(
            "init_shared_mem",
            FunctionType::get(Type::getVoidTy(Ctx), {Type::getInt32Ty(Ctx)}, false)
        );
        Builder.CreateCall(InitFunc, {ConstantInt::get(Type::getInt32Ty(Ctx), 0)});
        Builder.CreateRetVoid();
        
        appendToGlobalCtors(M, Ctor, 0);
    }

    PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM) {
        addGlobalInitializer(M);
        initBaseAddressGlobal(M);
        FunctionCallee AddCFEntry = getOrInsertAddControlFlowEntry(M);
        
        FunctionAnalysisManager &FAM = 
        AM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();


        for (Function &F : M) {
            if (F.isDeclaration()) continue;
            
            DominatorTree &DT = FAM.getResult<DominatorTreeAnalysis>(F);
            
            uint64_t funcHash = hashFunction(F);
            
            for (BasicBlock &BB : F) {
                for (Instruction &I : BB) {
                    if (shouldInstrument(I)) {
                        instrumentInstruction(I, F, DT, AddCFEntry, funcHash);
                    }
                }
            }
        }
        return PreservedAnalyses::none();
    }

private:
    void initBaseAddressGlobal(Module &M) {
        if (BaseAddrGV) return;
        
        LLVMContext &Ctx = M.getContext();
        BaseAddrGV = new GlobalVariable(
            M, Type::getInt64Ty(Ctx), false,
            GlobalValue::LinkOnceODRLinkage,
            ConstantInt::get(Type::getInt64Ty(Ctx), 0),
            "__cfi_module_base",
            nullptr,
            GlobalVariable::NotThreadLocal,
            0
        );
        BaseAddrGV->setAlignment(Align(8));
        BaseAddrGV->setVisibility(GlobalValue::HiddenVisibility);
    }

    uint64_t hashFunction(Function &F) {
        return std::hash<std::string>{}(F.getName().str());
    }

    bool shouldInstrument(Instruction &I) {
        if (auto *CB = dyn_cast<CallBase>(&I)) {
            if (Function *F = CB->getCalledFunction()) {
                if (F->isIntrinsic() || F->getName().starts_with("llvm."))
                    return false;
            }
        }
        return isa<CallBase>(I) || isa<IndirectBrInst>(I) || isa<ReturnInst>(I);
    }

    void instrumentInstruction(Instruction &I, Function &F, DominatorTree &DT,
                              FunctionCallee AddCFEntry, uint64_t funcHash) {
        IRBuilder<> Builder(&I);
        Value *TargetAddr = nullptr;

        if (auto *CI = dyn_cast<CallBase>(&I)) {
            TargetAddr = CI->getCalledOperand();
        } else if (auto *IBI = dyn_cast<IndirectBrInst>(&I)) {
            TargetAddr = IBI->getAddress();
        } else if (auto *RI = dyn_cast<ReturnInst>(&I)) {
            TargetAddr = emitReturnAddress(Builder, F.getContext());
        } else {
            return;
        }

        if (!TargetAddr->getType()->isPointerTy()) return;

        // 生成稳定的基地址计算
        Value *BaseAddr = Builder.CreateLoad(
            Type::getInt64Ty(F.getContext()), BaseAddrGV, "module_base");
        
        // 确保地址计算发生在当前指令之前
        Value *TargetInt = Builder.CreatePtrToInt(TargetAddr, Builder.getInt64Ty(), "target_int");
        Value *OffsetValue = Builder.CreateSub(TargetInt, BaseAddr, "cfi_offset");

        // 创建插桩调用
        Builder.CreateCall(AddCFEntry, {
            ConstantInt::get(Builder.getInt64Ty(), funcHash),
            OffsetValue
        });
    }

    Value* emitReturnAddress(IRBuilder<>& Builder, LLVMContext& Ctx) {
        Module* M = Builder.GetInsertBlock()->getModule();
        
        // 获取不透明指针类型（LLVM 15+默认模式）
        Type* RetAddrTy = PointerType::getUnqual(Ctx); // 使用上下文无关的指针类型
        
        // 声明returnaddress内置函数（返回类型为ptr）
        Function* ReturnAddrFunc = Intrinsic::getDeclaration(
            M,
            Intrinsic::returnaddress,
            { RetAddrTy }  // 关键：必须指定返回类型为ptr
        );
    
        // 生成调用指令
        CallInst* Call = Builder.CreateCall(
            ReturnAddrFunc->getFunctionType(),
            ReturnAddrFunc,
            { Builder.getInt32(0) },
            "retaddr"
        );
    
        // 设置属性（优化兼容性）
        Call->addFnAttr(Attribute::NoUnwind);
        Call->setCallingConv(CallingConv::C);
    
        return Call;
    }

    FunctionCallee getOrInsertAddControlFlowEntry(Module &M) {
        LLVMContext &Ctx = M.getContext();
        Type *ArgTypes[] = {Type::getInt64Ty(Ctx), Type::getInt64Ty(Ctx)};
        FunctionType *FuncType = FunctionType::get(Type::getVoidTy(Ctx), ArgTypes, false);
        return M.getOrInsertFunction("add_controlflow_entry", FuncType);
    }
};

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
    return {
        LLVM_PLUGIN_API_VERSION,
        "InstrumentIndirectInstructions",
        LLVM_VERSION_STRING,
        [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, ModulePassManager &MPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                    if (Name == "InstrumentIndirectInstructions") {
                        MPM.addPass(ControlFlowInstrumentPass());
                        return true;
                    }
                    return false;
                });
        }
    };
}