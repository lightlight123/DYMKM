#include "llvm/IR/PassManager.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Constants.h"

using namespace llvm;

class FindIndirectBranchPass : public PassInfoMixin<FindIndirectBranchPass> {
public:
    PreservedAnalyses run(Module &M, ModuleAnalysisManager &) {
        LLVMContext &Ctx = M.getContext();

        // 获取或创建 printf 函数声明
        FunctionCallee PrintfFunc = getOrInsertPrintf(M);

        for (Function &F : M) {
            for (BasicBlock &BB : F) {
                for (Instruction &I : BB) {
                    if (isTargetInstruction(I)) {
                        instrumentInstruction(I, PrintfFunc, Ctx, &F,&BB);
                    }
                }
            }
        }
        return PreservedAnalyses::none();
    }

private:
    //生成基本块ID的辅助函数
    std::string getBasicBlockID(BasicBlock *BB, Function *F) {
        int index = 0;
        for (auto &B : *F) {
            if (&B == BB) {
                return F->getName().str() + "_bb" + std::to_string(index);
            }
            index++;
        }
        return "unknown";
    }
    // 判断是否是间接跳转、间接调用或返回指令
    bool isTargetInstruction(Instruction &I) {
        if (auto *CI = dyn_cast<CallBase>(&I)) {
            if (CI->isIndirectCall()) {
                return true;
            }
        } else if (isa<IndirectBrInst>(I)) {
            return true;
        } else if (isa<ReturnInst>(I)) {
            return true;
        }
        return false;
    }

    // 插装指令以记录目标地址的偏移量
    void instrumentInstruction(Instruction &I, FunctionCallee PrintfFunc, 
        LLVMContext &Ctx, Function *Func, BasicBlock *BB) {
        IRBuilder<> Builder(&I);
        Type *IntType = Type::getInt64Ty(Ctx);

        // 获取源基本块ID
        std::string sourceID = getBasicBlockID(BB, Func);
        Constant *SourceIDStr = Builder.CreateGlobalStringPtr(sourceID);

        // 计算目标地址偏移量
        Value *TargetAddr = nullptr;
        if (auto *CI = dyn_cast<CallBase>(&I)) {
        TargetAddr = CI->getCalledOperand();
        } else if (auto *IBI = dyn_cast<IndirectBrInst>(&I)) {
        TargetAddr = IBI->getAddress();
        } else if (auto *RI = dyn_cast<ReturnInst>(&I)) {
        TargetAddr = emitReturnAddress(Builder, Ctx);
        }

        // 生成结构化输出
        if (TargetAddr) {
        Value *FuncStart = Builder.CreatePtrToInt(Func, IntType);
        Value *TargetInt = Builder.CreatePtrToInt(TargetAddr, IntType);
        Value *Offset = Builder.CreateSub(TargetInt, FuncStart);

        // 构造JSON格式输出
        Constant *FormatStr = Builder.CreateGlobalStringPtr(
        "{\"ID_source\":\"%s\",\"addrto_offset\":0x%lx}\n"
        );
        Builder.CreateCall(PrintfFunc, {
        FormatStr,
        SourceIDStr,
        Offset
        });
        }
    }

    void handleIndirectCall(CallBase *CI, IRBuilder<> &Builder, 
                           FunctionCallee PrintfFunc, Function *Func, Type *IntType) {
        Value *TargetAddr = CI->getCalledOperand();
        printAddressInfo("[Indirect Call]", TargetAddr, Builder, PrintfFunc, Func, IntType);
    }

    void handleIndirectBranch(IndirectBrInst *IBI, IRBuilder<> &Builder,
                            FunctionCallee PrintfFunc, Function *Func, Type *IntType) {
        Value *TargetAddr = IBI->getAddress();
        printAddressInfo("[Indirect Branch]", TargetAddr, Builder, PrintfFunc, Func, IntType);
    }

    void handleReturnInst(ReturnInst *RI, IRBuilder<> &Builder,
                         FunctionCallee PrintfFunc, Function *Func, 
                         Type *IntType, LLVMContext &Ctx) {
        Value *RetAddr = emitReturnAddress(Builder, Ctx);
        printAddressInfo("[Return]", RetAddr, Builder, PrintfFunc, Func, IntType);
    }

    // 新增：统一的地址信息打印函数
    void printAddressInfo(StringRef Prefix, Value *Addr, IRBuilder<> &Builder,
                         FunctionCallee PrintfFunc, Function *Func, Type *IntType) {
        // 打印绝对地址
        Constant *AbsFormat = Builder.CreateGlobalStringPtr(
            "%s Absolute Address: %p\n");
        Builder.CreateCall(PrintfFunc, {AbsFormat, 
                            Builder.CreateGlobalStringPtr(Prefix), Addr});

        // 计算并打印偏移量
        Value *FuncStart = Builder.CreatePtrToInt(Func, IntType);
        Value *TargetInt = Builder.CreatePtrToInt(Addr, IntType);
        Value *Offset = Builder.CreateSub(TargetInt, FuncStart);
        
        Constant *OffsetFormat = Builder.CreateGlobalStringPtr(
            "%s Offset: 0x%lx\n");
        Builder.CreateCall(PrintfFunc, {OffsetFormat, 
                            Builder.CreateGlobalStringPtr(Prefix), Offset});
    }
    // 生成代码以获取返回地址
    Value *emitReturnAddress(IRBuilder<> &Builder, LLVMContext &Ctx) {
        // 调用 LLVM 的 intrinsic 来获取返回地址
        Function *ReturnAddrFunc = Intrinsic::getDeclaration(
            Builder.GetInsertBlock()->getModule(), Intrinsic::returnaddress);
        return Builder.CreateCall(ReturnAddrFunc, {Builder.getInt32(0)});
    }

    // 获取或插入 printf 函数声明
    FunctionCallee getOrInsertPrintf(Module &M) {
        LLVMContext &Ctx = M.getContext();
        FunctionType *PrintfType = FunctionType::get(
            IntegerType::getInt32Ty(Ctx),
            PointerType::getUnqual(Type::getInt8Ty(Ctx)),
            true);
        return M.getOrInsertFunction("printf", PrintfType);
    }
};

// 插件接口实现
extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo findIndirectBranchPassPluginInfo() {
    return {
        LLVM_PLUGIN_API_VERSION, "FindIndirectBranchPass", LLVM_VERSION_STRING,
        [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, ModulePassManager &MPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                    if (Name == "InstrumentIndirectInstructions") {
                        MPM.addPass(FindIndirectBranchPass());
                        return true;
                    }
                    return false;
                });
        }
    };
}

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
    return findIndirectBranchPassPluginInfo();
}