#pragma once

#include "LLVMJITPrivate.h"
#include "WAVM/IR/Module.h"

PUSH_DISABLE_WARNINGS_FOR_LLVM_HEADERS
#include "llvm/IR/DIBuilder.h"
POP_DISABLE_WARNINGS_FOR_LLVM_HEADERS

namespace WAVM { namespace LLVMJIT {
	struct EmitModuleContext
	{
		const IR::Module& irModule;

		LLVMContext& llvmContext;
		llvm::Module* llvmModule;
		llvm::TargetMachine* targetMachine;
		bool useWindowsSEH;
		std::vector<llvm::Constant*> typeIds;
		std::vector<llvm::Function*> functions;
		std::vector<llvm::Constant*> tableOffsets;
		std::vector<llvm::Constant*> memoryOffsets;
		std::vector<llvm::Constant*> globals;
		std::vector<llvm::Constant*> exceptionTypeIds;

		llvm::Constant* defaultMemoryOffset;
		llvm::Constant* defaultTableOffset;

		llvm::Constant* moduleInstanceId;
		llvm::Constant* tableReferenceBias;

		llvm::DIBuilder diBuilder;
		llvm::DICompileUnit* diCompileUnit;
		llvm::DIFile* diModuleScope;

		llvm::DIType* diValueTypes[IR::numValueTypes];

		llvm::MDNode* likelyFalseBranchWeights;
		llvm::MDNode* likelyTrueBranchWeights;

		llvm::Value* fpRoundingModeMetadata;
		llvm::Value* fpExceptionMetadata;

		llvm::Function* cxaBeginCatchFunction = nullptr;
		llvm::Function* cxaEndCatchFunction = nullptr;
		llvm::Constant* runtimeExceptionTypeInfo = nullptr;

		EmitModuleContext(const IR::Module& inModule,
						  LLVMContext& inLLVMContext,
						  llvm::Module* inLLVMModule,
						  llvm::TargetMachine* inTargetMachine);

		inline llvm::Function* getLLVMIntrinsic(llvm::ArrayRef<llvm::Type*> typeArguments,
												llvm::Intrinsic::ID id)
		{
			return llvm::Intrinsic::getDeclaration(llvmModule, id, typeArguments);
		}
	};
}}
