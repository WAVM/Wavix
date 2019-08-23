#pragma once

#include "LLVMJITPrivate.h"
#include "WAVM/IR/Module.h"
#include "WAVM/IR/Types.h"
#include "WAVM/IR/Value.h"

PUSH_DISABLE_WARNINGS_FOR_LLVM_HEADERS
#include <llvm/IR/Constant.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/Value.h>
POP_DISABLE_WARNINGS_FOR_LLVM_HEADERS

namespace WAVM { namespace LLVMJIT {
	// Code and state that is used for generating IR for both thunks and WASM functions.
	struct EmitContext
	{
		LLVMContext& llvmContext;
		llvm::IRBuilder<> irBuilder;

		llvm::Value* contextPointerVariable;
		llvm::Value* memoryBasePointerVariable;

		EmitContext(LLVMContext& inLLVMContext, llvm::Constant* inDefaultMemoryOffset)
		: llvmContext(inLLVMContext)
		, irBuilder(inLLVMContext)
		, contextPointerVariable(nullptr)
		, memoryBasePointerVariable(nullptr)
		, defaultMemoryOffset(inDefaultMemoryOffset)
		{
		}

		llvm::Value* loadFromUntypedPointer(llvm::Value* pointer,
											llvm::Type* valueType,
											U32 alignment = 1)
		{
			auto load = irBuilder.CreateLoad(
				irBuilder.CreatePointerCast(pointer, valueType->getPointerTo()));
			load->setAlignment(alignment);
			return load;
		}

		void storeToUntypedPointer(llvm::Value* value, llvm::Value* pointer, U32 alignment = 1)
		{
			auto store = irBuilder.CreateStore(
				value, irBuilder.CreatePointerCast(pointer, value->getType()->getPointerTo()));
			store->setAlignment(alignment);
		}

		llvm::Value* getCompartmentAddress()
		{
			// Derive the compartment runtime data from the context address by masking off the lower
			// 31 bits.
			return irBuilder.CreateIntToPtr(
				irBuilder.CreateAnd(
					irBuilder.CreatePtrToInt(irBuilder.CreateLoad(contextPointerVariable),
											 llvmContext.i64Type),
					emitLiteral(llvmContext, ~((U64(1) << 31) - 1))),
				llvmContext.i8PtrType);
		}

		void reloadMemoryBase()
		{
			llvm::Value* compartmentAddress = getCompartmentAddress();

			// Load the defaultMemoryBase and defaultTableBase values from the runtime data for this
			// module instance.

			if(defaultMemoryOffset)
			{
				irBuilder.CreateStore(
					loadFromUntypedPointer(
						irBuilder.CreateInBoundsGEP(compartmentAddress, {defaultMemoryOffset}),
						llvmContext.i8PtrType,
						sizeof(U8*)),
					memoryBasePointerVariable);
			}
		}

		void initContextVariables(llvm::Value* initialContextPointer)
		{
			memoryBasePointerVariable
				= irBuilder.CreateAlloca(llvmContext.i8PtrType, nullptr, "memoryBase");
			contextPointerVariable
				= irBuilder.CreateAlloca(llvmContext.i8PtrType, nullptr, "context");
			irBuilder.CreateStore(initialContextPointer, contextPointerVariable);
			reloadMemoryBase();
		}

		// Creates either a call or an invoke if the call occurs inside a try.
		ValueVector emitCallOrInvoke(llvm::Value* callee,
									 llvm::ArrayRef<llvm::Value*> args,
									 IR::FunctionType calleeType,
									 IR::CallingConvention callingConvention,
									 llvm::BasicBlock* unwindToBlock = nullptr)
		{
			llvm::ArrayRef<llvm::Value*> callArgs = args;

			llvm::Value* resultsArray = nullptr;
			if(callingConvention == IR::CallingConvention::cAPICallback)
			{
				// Pass in pointers to argument and result arrays.
				auto argsArray = irBuilder.CreateAlloca(
					llvmContext.i8Type,
					emitLiteral(llvmContext, Uptr(args.size() * sizeof(IR::UntaggedValue))));
				for(Uptr argIndex = 0; argIndex < args.size(); ++argIndex)
				{
					storeToUntypedPointer(
						args[argIndex],
						irBuilder.CreateInBoundsGEP(
							argsArray,
							{emitLiteral(llvmContext,
										 Uptr(argIndex * sizeof(IR::UntaggedValue)))}));
				}

				resultsArray = irBuilder.CreateAlloca(
					llvmContext.i8Type,
					emitLiteral(llvmContext,
								Uptr(calleeType.results().size() * sizeof(IR::UntaggedValue))));

				auto callArgsAlloca = (llvm::Value**)alloca(sizeof(llvm::Value*) * 2);
				callArgs = llvm::ArrayRef<llvm::Value*>(callArgsAlloca, 2);
				callArgsAlloca[0] = argsArray;
				callArgsAlloca[1] = resultsArray;
			}
			else if(callingConvention != IR::CallingConvention::c)
			{
				// Augment the argument list with the context pointer.
				auto callArgsAlloca
					= (llvm::Value**)alloca(sizeof(llvm::Value*) * (args.size() + 1));
				callArgs = llvm::ArrayRef<llvm::Value*>(callArgsAlloca, args.size() + 1);
				callArgsAlloca[0] = irBuilder.CreateLoad(contextPointerVariable);
				for(Uptr argIndex = 0; argIndex < args.size(); ++argIndex)
				{ callArgsAlloca[1 + argIndex] = args[argIndex]; }
			}

			// Call or invoke the callee.
			llvm::Value* returnValue;
			if(!unwindToBlock)
			{
				auto call = irBuilder.CreateCall(callee, callArgs);
				call->setCallingConv(asLLVMCallingConv(callingConvention));
				returnValue = call;
			}
			else
			{
				auto returnBlock = llvm::BasicBlock::Create(
					llvmContext, "invokeReturn", irBuilder.GetInsertBlock()->getParent());
				auto invoke = irBuilder.CreateInvoke(callee, returnBlock, unwindToBlock, callArgs);
				invoke->setCallingConv(asLLVMCallingConv(callingConvention));
				irBuilder.SetInsertPoint(returnBlock);
				returnValue = invoke;
			}

			ValueVector results;
			switch(callingConvention)
			{
			case IR::CallingConvention::wasm: {
				// Update the context variable.
				auto newContextPointer = irBuilder.CreateExtractValue(returnValue, {0});
				irBuilder.CreateStore(newContextPointer, contextPointerVariable);

				// Reload the memory/table base pointers.
				reloadMemoryBase();

				if(areResultsReturnedDirectly(calleeType.results()))
				{
					// If the results are returned directly, extract them from the returned struct.
					for(Uptr resultIndex = 0; resultIndex < calleeType.results().size();
						++resultIndex)
					{
						results.push_back(
							irBuilder.CreateExtractValue(returnValue, {U32(1), U32(resultIndex)}));
					}
				}
				else
				{
					// Otherwise, load them from the context.
					Uptr resultOffset = 0;
					for(IR::ValueType resultType : calleeType.results())
					{
						const U8 resultNumBytes = getTypeByteWidth(resultType);

						resultOffset = (resultOffset + resultNumBytes - 1) & -I8(resultNumBytes);
						WAVM_ASSERT(resultOffset < Runtime::maxThunkArgAndReturnBytes);

						results.push_back(loadFromUntypedPointer(
							irBuilder.CreateInBoundsGEP(newContextPointer,
														{emitLiteral(llvmContext, resultOffset)}),
							asLLVMType(llvmContext, resultType),
							resultNumBytes));

						resultOffset += resultNumBytes;
					}
				}

				break;
			}
			case IR::CallingConvention::intrinsicWithContextSwitch: {
				auto newContextPointer = returnValue;

				// Update the context variable.
				irBuilder.CreateStore(newContextPointer, contextPointerVariable);

				// Reload the memory/table base pointers.
				reloadMemoryBase();

				// Load the call result from the returned context.
				WAVM_ASSERT(calleeType.results().size() <= 1);
				if(calleeType.results().size() == 1)
				{
					llvm::Type* llvmResultType = asLLVMType(llvmContext, calleeType.results()[0]);
					results.push_back(
						loadFromUntypedPointer(newContextPointer,
											   llvmResultType,
											   IR::getTypeByteWidth(calleeType.results()[0])));
				}

				break;
			}
			case IR::CallingConvention::cAPICallback: {
				// Check whether the call returned an Exception.
				auto exception = returnValue;
				auto function = irBuilder.GetInsertBlock()->getParent();
				auto trapBlock = llvm::BasicBlock::Create(llvmContext, "intrinsicTrap", function);
				auto returnBlock
					= llvm::BasicBlock::Create(llvmContext, "intrinsicReturn", function);
				irBuilder.CreateCondBr(
					irBuilder.CreateICmpEQ(exception,
										   llvm::Constant::getNullValue(llvmContext.i8PtrType)),
					returnBlock,
					trapBlock);

				irBuilder.SetInsertPoint(trapBlock);
				irBuilder.CreateUnreachable();

				// Load the results from the results array.
				irBuilder.SetInsertPoint(returnBlock);
				for(Uptr resultIndex = 0; resultIndex < calleeType.results().size(); ++resultIndex)
				{
					results.push_back(loadFromUntypedPointer(
						irBuilder.CreateInBoundsGEP(
							resultsArray,
							{emitLiteral(llvmContext, resultIndex * sizeof(IR::UntaggedValue))}),
						asLLVMType(llvmContext, calleeType.results()[resultIndex])));
				}

				break;
			}
			case IR::CallingConvention::intrinsic:
			case IR::CallingConvention::c: {
				WAVM_ASSERT(calleeType.results().size() <= 1);
				if(calleeType.results().size() == 1) { results.push_back(returnValue); }
				break;
			}
			default: WAVM_UNREACHABLE();
			};

			return results;
		}

		void emitReturn(IR::TypeTuple resultTypes, const llvm::ArrayRef<llvm::Value*>& results)
		{
			llvm::Value* returnStruct = getZeroedLLVMReturnStruct(llvmContext, resultTypes);
			returnStruct = irBuilder.CreateInsertValue(
				returnStruct, irBuilder.CreateLoad(contextPointerVariable), {U32(0)});

			WAVM_ASSERT(resultTypes.size() == results.size());
			if(areResultsReturnedDirectly(resultTypes))
			{
				// If the results are returned directly, insert them into the return struct.
				for(Uptr resultIndex = 0; resultIndex < results.size(); ++resultIndex)
				{
					llvm::Value* result = results[resultIndex];
					returnStruct = irBuilder.CreateInsertValue(
						returnStruct, result, {U32(1), U32(resultIndex)});
				}
			}
			else
			{
				// Otherwise, store them in the context.
				Uptr resultOffset = 0;
				for(Uptr resultIndex = 0; resultIndex < results.size(); ++resultIndex)
				{
					const IR::ValueType resultType = resultTypes[resultIndex];
					const U8 resultNumBytes = IR::getTypeByteWidth(resultType);

					resultOffset = (resultOffset + resultNumBytes - 1) & -I8(resultNumBytes);
					WAVM_ASSERT(resultOffset < Runtime::maxThunkArgAndReturnBytes);

					irBuilder.CreateStore(results[resultIndex],
										  irBuilder.CreatePointerCast(
											  irBuilder.CreateInBoundsGEP(
												  irBuilder.CreateLoad(contextPointerVariable),
												  {emitLiteral(llvmContext, resultOffset)}),
											  asLLVMType(llvmContext, resultType)->getPointerTo()));

					resultOffset += resultNumBytes;
				}
			}

			irBuilder.CreateRet(returnStruct);
		}

	private:
		llvm::Constant* defaultMemoryOffset;
	};
}}
