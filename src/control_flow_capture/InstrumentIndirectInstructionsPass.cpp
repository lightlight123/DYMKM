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
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/DerivedTypes.h>

using namespace llvm;

extern "C" {
    void init_shared_mem(int is_creator);
    void add_controlflow_entry(uint64_t source_bbid, uint64_t src_module_base, uint64_t target_offset);
}

class ControlFlowInstrumentPass : public PassInfoMixin<ControlFlowInstrumentPass> {
private:
    GlobalVariable *SrcBaseGV = nullptr;
    GlobalVariable *TargetBaseGV = nullptr;
    DenseMap<Function*, unsigned> BBIDCounter;

    uint64_t getBasicBlockID(BasicBlock &BB) {
        Function *F = BB.getParent();
        if (!BBIDCounter.count(F)) BBIDCounter[F] = 0;
    
        uint64_t funcHash = hashFunction(*F);
        unsigned id = BBIDCounter[F]++;
        return (funcHash << 32) | id;
    }

    void initBaseAddressGlobal(Module &M, bool isSource) {
        GlobalVariable **GV = isSource ? &SrcBaseGV : &TargetBaseGV;
        if (*GV) return;
    
        LLVMContext &Ctx = M.getContext();
        *GV = new GlobalVariable(
            M, Type::getInt64Ty(Ctx), false,
            GlobalValue::LinkOnceODRLinkage,
            ConstantInt::get(Type::getInt64Ty(Ctx), 0),
            isSource ? "__src_module_base" : "__target_module_base"
        );
        (*GV)->setAlignment(Align(8));
        (*GV)->setVisibility(GlobalValue::HiddenVisibility);
    }
    
    void emitBaseAddressInit(IRBuilder<> &Builder, GlobalVariable *GV, bool isSource) {
        Module *M = Builder.GetInsertBlock()->getModule();
        StringRef AnchorName = isSource ? "__src_module_anchor" : "__target_module_anchor";
        
        Function *AnchorFunc = M->getFunction(AnchorName);
        if (!AnchorFunc) {
            AnchorFunc = Function::Create(
                FunctionType::get(Type::getVoidTy(M->getContext()), false),
                GlobalValue::InternalLinkage,
                AnchorName,
                M
            );
            BasicBlock *Entry = BasicBlock::Create(M->getContext(), "entry", AnchorFunc);
            IRBuilder<> AnchorBuilder(Entry);
            AnchorBuilder.CreateRetVoid();
        }
    
        Value *BaseAddr = Builder.CreatePtrToInt(AnchorFunc, Builder.getInt64Ty());
        Builder.CreateStore(BaseAddr, GV);
    }

public:
    void addGlobalInitializer(Module &M) {
        if (M.getFunction("cf_initializer")) return;

        LLVMContext &Ctx = M.getContext();
        IRBuilder<> Builder(Ctx);
        
        Function *Ctor = Function::Create(
            FunctionType::get(Builder.getVoidTy(), false),
            GlobalValue::InternalLinkage,
            "cf_initializer",
            &M
        );
        BasicBlock *BB = BasicBlock::Create(Ctx, "entry", Ctor);
        Builder.SetInsertPoint(BB);

        initBaseAddressGlobal(M, true);
        emitBaseAddressInit(Builder, SrcBaseGV, true);
        initBaseAddressGlobal(M, false);
        emitBaseAddressInit(Builder, TargetBaseGV, false);

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
        assert(SrcBaseGV && TargetBaseGV && "Global variables not initialized!");

        FunctionCallee AddCFEntry = getOrInsertAddControlFlowEntry(M);
        for (Function &F : M) {
            if (F.isDeclaration()) continue;
            for (BasicBlock &BB : F) {
                uint64_t bbID = getBasicBlockID(BB);
                for (Instruction &I : BB) {
                    if (shouldInstrument(I)) {
                        instrumentInstruction(I, bbID, AddCFEntry);
                    }
                }
            }
        }
        return PreservedAnalyses::none();
    }

private:
    void emitDebugInfo(IRBuilder<> &Builder, uint64_t bbID, 
        Value* SrcBase, Value* TargetBase, Value* TargetAddr, Value* TargetOffset) {
        Module *M = Builder.GetInsertBlock()->getModule();
        LLVMContext &Ctx = M->getContext();

        PointerType *PrintfArgTy = PointerType::getUnqual(Type::getInt8Ty(Ctx));
        FunctionType *PrintfTy = FunctionType::get(
            Type::getInt32Ty(Ctx), PrintfArgTy, true);
        FunctionCallee PrintfFn = M->getOrInsertFunction("printf", PrintfTy);

        Constant *FormatStr = Builder.CreateGlobalStringPtr(
            "[DEBUG] BBID=0x%lx\n"
            "  SrcBase=0x%lx\n"
            "  TargetBase=0x%lx\n"
            "  TargetAddr=0x%lx\n"
            "  Offset=0x%lx (验证结果: %s)\n\n");

        Value *ComputedOffset = Builder.CreateSub(TargetAddr, TargetBase);
        Value *OffsetValid = Builder.CreateICmpEQ(ComputedOffset, TargetOffset);
        Value *ValidStr = Builder.CreateSelect(OffsetValid,
            Builder.CreateGlobalStringPtr("有效"),
            Builder.CreateGlobalStringPtr("无效"));

        Value* Args[] = {
            FormatStr,
            ConstantInt::get(Builder.getInt64Ty(), bbID),
            SrcBase,
            TargetBase,
            TargetAddr,
            TargetOffset,
            ValidStr
        };

        Builder.CreateCall(PrintfFn, Args);
    }

    bool shouldInstrument(Instruction &I) {
        // 仅保留间接函数调用、间接跳转、返回指令
        if (auto *CI = dyn_cast<CallBase>(&I)) {
            return CI->isIndirectCall(); // 仅捕获间接调用
        }
        return isa<IndirectBrInst>(I) || isa<ReturnInst>(I); // 捕获间接跳转和返回指令
    }
    
    void instrumentInstruction(Instruction &I, uint64_t bbID, FunctionCallee AddCFEntry) {
        IRBuilder<> Builder(&I);
        Value *TargetAddr = nullptr;
    
        // 处理间接函数调用
        if (auto *CI = dyn_cast<CallBase>(&I)) {
            if (CI->isIndirectCall()) { // 确保是间接调用
                TargetAddr = CI->getCalledOperand();
            }
        } 
        // 处理间接跳转
        else if (auto *IBI = dyn_cast<IndirectBrInst>(&I)) {
            TargetAddr = IBI->getAddress();
        } 
        // 处理返回指令
        else if (auto *RI = dyn_cast<ReturnInst>(&I)) {
            TargetAddr = getReturnAddress(Builder);
        }
    
        // 如果没有目标地址或者目标地址不是指针类型，则不进行插装
        if (!TargetAddr || !TargetAddr->getType()->isPointerTy()) return;
    
        // 将指针转换为整数类型
        Value *TargetInt = Builder.CreatePtrToInt(TargetAddr, Builder.getInt64Ty());
        Value *SrcBase = Builder.CreateLoad(SrcBaseGV->getValueType(), SrcBaseGV);
        Value *TargetBase = Builder.CreateLoad(TargetBaseGV->getValueType(), TargetBaseGV);
        Value *TargetOffset = Builder.CreateSub(TargetInt, TargetBase);
    
        // 记录调试信息
        emitDebugInfo(Builder, bbID, SrcBase, TargetBase, TargetInt, TargetOffset);
    
        // 生成插装代码，调用 AddCFEntry 记录控制流信息
        Builder.CreateCall(AddCFEntry, {
            ConstantInt::get(Builder.getInt64Ty(), bbID),
            SrcBase,
            TargetOffset
        });
    }
    
    Value *getReturnAddress(IRBuilder<> &Builder) {
        Function *ReturnAddrFn = Intrinsic::getDeclaration(
            Builder.GetInsertBlock()->getModule(), 
            Intrinsic::returnaddress
        );
        return Builder.CreateCall(ReturnAddrFn, {Builder.getInt32(0)});
    }

    FunctionCallee getOrInsertAddControlFlowEntry(Module &M) {
        LLVMContext &Ctx = M.getContext();
        Type *ArgTypes[] = {Type::getInt64Ty(Ctx), Type::getInt64Ty(Ctx), Type::getInt64Ty(Ctx)};
        FunctionType *FuncType = FunctionType::get(Type::getVoidTy(Ctx), ArgTypes, false);
        return M.getOrInsertFunction("add_controlflow_entry", FuncType);
    }

    uint64_t hashFunction(Function &F) {
        std::string Identifier = F.getName().str() + 
                               F.getParent()->getSourceFileName() + 
                               std::to_string(reinterpret_cast<uintptr_t>(&F));
        return std::hash<std::string>{}(Identifier);
    }
};

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
    return {
        LLVM_PLUGIN_API_VERSION,
        "CFGInstrumentation",
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