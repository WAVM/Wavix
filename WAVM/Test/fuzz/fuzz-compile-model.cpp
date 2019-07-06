#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <memory>
#include <utility>
#include <vector>

#include "WAVM/IR/IR.h"
#include "WAVM/IR/Module.h"
#include "WAVM/IR/Operators.h"
#include "WAVM/IR/Types.h"
#include "WAVM/IR/Validate.h"
#include "WAVM/Inline/Assert.h"
#include "WAVM/Inline/BasicTypes.h"
#include "WAVM/Inline/Config.h"
#include "WAVM/Inline/Errors.h"
#include "WAVM/Inline/Hash.h"
#include "WAVM/Inline/HashMap.h"
#include "WAVM/Inline/Serialization.h"
#include "WAVM/LLVMJIT/LLVMJIT.h"
#include "WAVM/Logging/Logging.h"

using namespace WAVM;
using namespace WAVM::IR;
using namespace WAVM::Runtime;

// A stream that uses a combination of a PRNG and input data to produce pseudo-random values.
struct RandomStream
{
	RandomStream(const U8* inData, Uptr numBytes)
	: next(inData), end(inData + numBytes), denominator(0), numerator(0), seed(0)
	{
		refill();
	}

	// Returns a pseudo-random value between 0 and maxResult, inclusive.
	template<typename Result> Result get(Result maxResult)
	{
		Result result = Result(get64(maxResult));
		wavmAssert(result <= maxResult);
		return result;
	}

private:
	const U8* next;
	const U8* end;

	U64 denominator;
	U64 numerator;

	U64 seed;

	void refill()
	{
		while(denominator <= UINT32_MAX)
		{
			if(next < end) { numerator += (denominator + 1) * *next++; }
			denominator += 255 * (denominator + 1);
		};
	}

	U32 get32(U32 maxResult)
	{
		if(maxResult == 0) { return 0; }

		wavmAssert(denominator >= maxResult);
		seed ^= numerator;
		const U32 result = U32(seed % (U64(maxResult) + 1));
		seed /= (U64(maxResult) + 1);
		numerator /= (U64(maxResult) + 1);
		denominator /= (U64(maxResult) + 1);
		seed = 6364136223846793005 * seed + 1442695040888963407;
		refill();
		return result;
	}

	U64 get64(U64 maxResult)
	{
		U64 result = get32(U32(maxResult));
		result += U64(get32(U32(maxResult >> 32))) << 32;
		wavmAssert(result <= maxResult);
		return result;
	}
};

static void generateImm(RandomStream& random, IR::Module& module, NoImm& outImm) {}
static void generateImm(RandomStream& random, IR::Module& module, MemoryImm& outImm)
{
	outImm.memoryIndex = random.get(module.memories.size() - 1);
}
static void generateImm(RandomStream& random, IR::Module& module, MemoryCopyImm& outImm)
{
	outImm.sourceMemoryIndex = random.get(module.memories.size() - 1);
	outImm.destMemoryIndex = random.get(module.memories.size() - 1);
}
static void generateImm(RandomStream& random, IR::Module& module, TableImm& outImm)
{
	outImm.tableIndex = random.get(module.tables.size() - 1);
}
static void generateImm(RandomStream& random, IR::Module& module, TableCopyImm& outImm)
{
	outImm.sourceTableIndex = random.get(module.tables.size() - 1);
	outImm.destTableIndex = random.get(module.tables.size() - 1);
}
static void generateImm(RandomStream& random, IR::Module& module, FunctionImm& outImm)
{
	outImm.functionIndex = random.get(module.functions.size() - 1);
}

static void generateImm(RandomStream& random, IR::Module& module, LiteralImm<I32>& outImm)
{
	outImm.value = I32(random.get(UINT32_MAX));
}

static void generateImm(RandomStream& random, IR::Module& module, LiteralImm<I64>& outImm)
{
	outImm.value = I64(random.get(UINT64_MAX));
}

static void generateImm(RandomStream& random, IR::Module& module, LiteralImm<F32>& outImm)
{
	const U32 u32 = random.get(UINT32_MAX);
	memcpy(&outImm.value, &u32, sizeof(U32));
}

static void generateImm(RandomStream& random, IR::Module& module, LiteralImm<F64>& outImm)
{
	const U64 u64 = random.get(UINT64_MAX);
	memcpy(&outImm.value, &u64, sizeof(U64));
}

static void generateImm(RandomStream& random, IR::Module& module, LiteralImm<V128>& outImm)
{
	outImm.value.u64[0] = random.get(UINT64_MAX);
	outImm.value.u64[1] = random.get(UINT64_MAX);
}

template<Uptr naturalAlignmentLog2>
static void generateImm(RandomStream& random,
						IR::Module& module,
						LoadOrStoreImm<naturalAlignmentLog2>& outImm)
{
	outImm.alignmentLog2 = random.get<U8>(naturalAlignmentLog2);
	outImm.offset = random.get(UINT32_MAX);
}

template<Uptr naturalAlignmentLog2>
static void generateImm(RandomStream& random,
						IR::Module& module,
						AtomicLoadOrStoreImm<naturalAlignmentLog2>& outImm)
{
	outImm.alignmentLog2 = naturalAlignmentLog2;
	outImm.offset = random.get(UINT32_MAX);
}

template<Uptr numLanes>
static void generateImm(RandomStream& random, IR::Module& module, LaneIndexImm<numLanes>& outImm)
{
	outImm.laneIndex = random.get<U8>(numLanes - 1);
}

template<Uptr numLanes>
static void generateImm(RandomStream& random, IR::Module& module, ShuffleImm<numLanes>& outImm)
{
	for(Uptr laneIndex = 0; laneIndex < numLanes; ++laneIndex)
	{ outImm.laneIndices[laneIndex] = random.get<U8>(numLanes * 2 - 1); }
}

static void generateImm(RandomStream& random, IR::Module& module, DataSegmentAndMemImm& outImm)
{
	outImm.dataSegmentIndex = random.get(module.dataSegments.size() - 1);
	outImm.memoryIndex = random.get(module.memories.size() - 1);
}

static void generateImm(RandomStream& random, IR::Module& module, DataSegmentImm& outImm)
{
	outImm.dataSegmentIndex = random.get(module.dataSegments.size() - 1);
}

static void generateImm(RandomStream& random, IR::Module& module, ElemSegmentAndTableImm& outImm)
{
	outImm.elemSegmentIndex = random.get(module.elemSegments.size() - 1);
	outImm.tableIndex = random.get(module.tables.size() - 1);
}

static void generateImm(RandomStream& random, IR::Module& module, ElemSegmentImm& outImm)
{
	outImm.elemSegmentIndex = random.get(module.elemSegments.size() - 1);
}

// Build a table with information about non-parametric operators.

typedef CodeValidationProxyStream<OperatorEncoderStream> CodeStream;
typedef void OperatorEmitFunc(RandomStream&, IR::Module&, CodeStream&);

struct OperatorInfo
{
	const char* name;
	FunctionType (*sig)();
	OperatorEmitFunc* emit;
};

#define VISIT_OP(encoding, name, nameString, Imm, SIGNATURE, ...)                                  \
	{nameString,                                                                                   \
	 []() { return IR::getNonParametricOpSigs().name; },                                           \
	 [](RandomStream& random, IR::Module& module, CodeStream& codeStream) {                        \
		 Imm imm;                                                                                  \
		 generateImm(random, module, imm);                                                         \
		 codeStream.name(imm);                                                                     \
	 }},
static OperatorInfo operatorInfos[]{WAVM_ENUM_NONCONTROL_NONPARAMETRIC_OPERATORS(VISIT_OP)};
#undef VISIT_OP

enum
{
	numNonParametricOps = sizeof(operatorInfos) / sizeof(OperatorInfo)
};

static ValueType generateValueType(RandomStream& random)
{
	switch(random.get(6))
	{
	case 0: return ValueType::i32;
	case 1: return ValueType::i64;
	case 2: return ValueType::f32;
	case 3: return ValueType::f64;
	case 4: return ValueType::v128;
	case 5: return ValueType::anyref;
	case 6: return ValueType::funcref;
	default: WAVM_UNREACHABLE();
	}
}

FunctionType generateBlockSig(RandomStream& random, TypeTuple params)
{
	// Generalize the params to map internal type subsets (e.g. nullref) to a canonical type.
	ValueType* remappedParamsVector = (ValueType*)alloca(sizeof(ValueType*) * params.size());
	for(Uptr index = 0; index < params.size(); ++index)
	{
		remappedParamsVector[index]
			= params[index] == ValueType::nullref ? ValueType::anyref : params[index];
	}
	params = TypeTuple(remappedParamsVector, params.size());

	const Uptr maxResults = 4;
	ValueType results[maxResults];
	const Uptr numResults = random.get(4);
	for(Uptr resultIndex = 0; resultIndex < numResults; ++resultIndex)
	{ results[resultIndex] = generateValueType(random); }

	return FunctionType(TypeTuple(results, numResults), params);
}

IndexedBlockType getIndexedBlockType(IR::Module& module,
									 HashMap<FunctionType, Uptr>& functionTypeMap,
									 const FunctionType sig)
{
	if(sig.params().size() || sig.results().size() > 1)
	{
		IndexedBlockType result;
		result.format = IndexedBlockType::functionType;
		result.index = functionTypeMap.getOrAdd(sig, module.types.size());
		if(result.index == module.types.size()) { module.types.push_back(sig); }
		return result;
	}
	else
	{
		return sig.results().size() == 1
				   ? IndexedBlockType{IndexedBlockType::Format::oneResult, {sig.results()[0]}}
				   : IndexedBlockType{IndexedBlockType::Format::noParametersOrResult, {}};
	}
}

struct ControlContext
{
	enum class Type : U8
	{
		function,
		block,
		ifThen,
		ifElse,
		loop,
		try_,
		catch_
	};

	Type type;
	Uptr outerStackSize;

	TypeTuple params;
	TypeTuple results;

	TypeTuple elseParams;
};

static void emitControlEnd(std::vector<ControlContext>& controlStack,
						   std::vector<ValueType>& stack,
						   IR::Module& module,
						   CodeStream& codeStream)
{
	if(controlStack.back().type == ControlContext::Type::ifThen)
	{
		// Emit the else operator.
		codeStream.else_();

		stack.resize(controlStack.back().outerStackSize);
		for(ValueType elseParam : controlStack.back().elseParams) { stack.push_back(elseParam); }

		// Change the current control context type to an else clause.
		controlStack.back().type = ControlContext::Type::ifElse;
	}
	else
	{
		if(controlStack.back().type == ControlContext::Type::try_
		   || controlStack.back().type == ControlContext::Type::catch_)
		{
			// TODO: catch
			WAVM_UNREACHABLE();
		}

		codeStream.end();
		stack.resize(controlStack.back().outerStackSize);
		for(ValueType result : controlStack.back().results) { stack.push_back(result); }
		controlStack.pop_back();
	}
}

static void generateFunction(RandomStream& random,
							 IR::Module& module,
							 FunctionDef& functionDef,
							 HashMap<FunctionType, Uptr>& functionTypeMap)
{
	const FunctionType functionType = module.types[functionDef.type.index];

	// Generate locals.
	const Uptr numNonParameterLocals = random.get(4);
	for(Uptr localIndex = 0; localIndex < numNonParameterLocals; ++localIndex)
	{ functionDef.nonParameterLocalTypes.push_back(generateValueType(random)); }
	const Uptr numLocals = functionType.params().size() + numNonParameterLocals;

	Serialization::ArrayOutputStream codeByteStream;
	OperatorEncoderStream opEncoder(codeByteStream);
	CodeValidationProxyStream<OperatorEncoderStream> codeStream(module, functionDef, opEncoder);

	std::vector<ControlContext> controlStack;
	controlStack.push_back({ControlContext::Type::function,
							0,
							functionType.results(),
							functionType.results(),
							TypeTuple()});

	std::vector<ValueType> stack;

	Uptr numInstructions = 0;

	std::vector<std::function<OperatorEmitFunc>> validOpEmitters;
	while(controlStack.size())
	{
		bool allowStackGrowth
			= stack.size() - controlStack.back().outerStackSize <= 6 && numInstructions++ < 30;

		if(stack.size() <= controlStack.back().outerStackSize + controlStack.back().results.size())
		{
			const ControlContext& controlContext = controlStack.back();

			bool sigMatches = true;
			for(Uptr resultIndex = 0; resultIndex < controlContext.results.size(); ++resultIndex)
			{
				if(controlContext.outerStackSize + resultIndex >= stack.size())
				{
					sigMatches = false;
					allowStackGrowth = true;
				}
				else if(!isSubtype(stack[controlContext.outerStackSize + resultIndex],
								   controlContext.results[resultIndex]))
				{
					sigMatches = false;
					break;
				}
			}

			if(sigMatches)
			{
				if(controlContext.type == ControlContext::Type::ifThen)
				{
					// Enter an if-else clause.
					validOpEmitters.push_back([&stack, &controlStack](RandomStream& random,
																	  IR::Module& module,
																	  CodeStream& codeStream) {
						// Emit the else operator.
						codeStream.else_();

						stack.resize(controlStack.back().outerStackSize);
						for(ValueType elseParam : controlStack.back().elseParams)
						{ stack.push_back(elseParam); }

						// Change the current control context type to an else clause.
						controlStack.back().type = ControlContext::Type::ifElse;
					});
				}

				if(controlContext.type != ControlContext::Type::try_
				   && (controlContext.type != ControlContext::Type::ifThen
					   || controlContext.elseParams == controlStack.back().results))
				{
					// End the current control structure.
					validOpEmitters.push_back([&stack, &controlStack](RandomStream& random,
																	  IR::Module& module,
																	  CodeStream& codeStream) {
						// Emit the end operator.
						codeStream.end();

						// Push the control context's results on the stack.
						stack.resize(controlStack.back().outerStackSize);
						const TypeTuple& results = controlStack.back().results;
						stack.insert(stack.end(), results.begin(), results.end());

						// Pop the control stack.
						controlStack.pop_back();
					});
				}
			}
		}

		// Build a list of the non-parametric operators that are valid given the current state of
		// the stack.
		for(Uptr opIndex = 0; opIndex < numNonParametricOps; ++opIndex)
		{
			const OperatorInfo& opInfo = operatorInfos[opIndex];
			const TypeTuple params = opInfo.sig().params();
			const TypeTuple results = opInfo.sig().results();

			// If the random stream has run out of entropy, only consider operators that result in
			// fewer operands on the stack.
			if(!allowStackGrowth && results.size() >= params.size()) { continue; }

			// Ensure the stack has enough values for the operator's parameters.
			if(params.size() > stack.size() - controlStack.back().outerStackSize) { continue; }

			// Check that the types of values on top of the stack are the right type for the
			// operator's parameters.
			bool sigMatch = true;
			for(Uptr paramIndex = 0; paramIndex < params.size(); ++paramIndex)
			{
				if(!isSubtype(stack[stack.size() - params.size() + paramIndex], params[paramIndex]))
				{
					sigMatch = false;
					break;
				}
			}
			if(sigMatch)
			{
				// Add the operator to the list of valid operators.
				validOpEmitters.push_back([&stack, opInfo](RandomStream& random,
														   IR::Module& module,
														   CodeStream& codeStream) {
					opInfo.emit(random, module, codeStream);

					// Remove the operator's parameters from the top of the stack.
					stack.resize(stack.size() - opInfo.sig().params().size());

					// Push the operator's results onto the stack.
					for(ValueType result : opInfo.sig().results()) { stack.push_back(result); }
				});
			}
		}

		// Build a list of the parametric operators that are valid given the current state of the
		// stack.

		for(Uptr localIndex = 0; localIndex < numLocals; ++localIndex)
		{
			const ValueType localType
				= localIndex < functionType.params().size()
					  ? functionType.params()[localIndex]
					  : functionDef
							.nonParameterLocalTypes[localIndex - functionType.params().size()];

			if(stack.size() > controlStack.back().outerStackSize
			   && isSubtype(stack.back(), localType))
			{
				// local.set
				validOpEmitters.push_back([&stack, localIndex](RandomStream& random,
															   IR::Module& module,
															   CodeStream& codeStream) {
					codeStream.local_set({localIndex});
					stack.pop_back();
				});

				// local.tee
				if(allowStackGrowth)
				{
					validOpEmitters.push_back([localIndex](RandomStream& random,
														   IR::Module& module,
														   CodeStream& codeStream) {
						codeStream.local_tee({localIndex});
					});
				}
			}

			// local.get
			if(allowStackGrowth)
			{
				validOpEmitters.push_back([&stack, localIndex, localType](RandomStream& random,
																		  IR::Module& module,
																		  CodeStream& codeStream) {
					codeStream.local_get({localIndex});
					stack.push_back(localType);
				});
			}
		}

		for(Uptr globalIndex = 0; globalIndex < module.globals.size(); ++globalIndex)
		{
			const GlobalType globalType = module.globals.getType(globalIndex);

			if(stack.size() > controlStack.back().outerStackSize
			   && isSubtype(stack.back(), globalType.valueType) && globalType.isMutable)
			{
				// global.set
				validOpEmitters.push_back([&stack, globalIndex](RandomStream& random,
																IR::Module& module,
																CodeStream& codeStream) {
					codeStream.global_set({globalIndex});
					stack.pop_back();
				});
			}

			if(allowStackGrowth)
			{
				// global.get
				validOpEmitters.push_back(
					[&stack, globalIndex, globalType](
						RandomStream& random, IR::Module& module, CodeStream& codeStream) {
						codeStream.global_get({globalIndex});
						stack.push_back(globalType.valueType);
					});
			}
		}

		for(Uptr tableIndex = 0; tableIndex < module.tables.size(); ++tableIndex)
		{
			const TableType tableType = module.tables.getType(tableIndex);

			if(stack.size() - controlStack.back().outerStackSize >= 2
			   && stack[stack.size() - 2] == ValueType::i32
			   && isSubtype(stack.back(), asValueType(tableType.elementType)))
			{
				// table.set
				validOpEmitters.push_back([&stack, tableIndex](RandomStream& random,
															   IR::Module& module,
															   CodeStream& codeStream) {
					codeStream.table_set({U32(tableIndex)});
					stack.resize(stack.size() - 2);
				});
			}

			if(stack.size() > controlStack.back().outerStackSize && stack.back() == ValueType::i32)
			{
				// table.get
				validOpEmitters.push_back([&stack, tableIndex, tableType](RandomStream& random,
																		  IR::Module& module,
																		  CodeStream& codeStream) {
					codeStream.table_get({tableIndex});
					stack.pop_back();
					stack.push_back(asValueType(tableType.elementType));
				});

				if(tableType.elementType == ReferenceType::funcref)
				{
					// call_indirect
					for(Uptr typeIndex = 0; typeIndex < module.types.size(); ++typeIndex)
					{
						const FunctionType calleeType = module.types[typeIndex];
						const TypeTuple params = calleeType.params();
						const TypeTuple results = calleeType.results();

						// If the random stream has run out of entropy, only consider operators that
						// result in fewer operands on the stack.
						if(!allowStackGrowth && results.size() >= params.size() + 1) { continue; }

						// Ensure the stack has enough values for the operator's parameters.
						if(params.size() + 1 > stack.size() - controlStack.back().outerStackSize)
						{ continue; }

						// Check that the types of values on top of the stack are the right type for
						// the operator's parameters.
						bool sigMatch = true;
						for(Uptr paramIndex = 0; paramIndex < params.size(); ++paramIndex)
						{
							if(stack[stack.size() - params.size() + paramIndex - 1]
							   != params[paramIndex])
							{
								sigMatch = false;
								break;
							}
						}
						if(sigMatch)
						{
							validOpEmitters.push_back([&stack, calleeType, typeIndex, tableIndex](
														  RandomStream& random,
														  IR::Module& module,
														  CodeStream& codeStream) {
								codeStream.call_indirect({{typeIndex}, tableIndex});

								// Remove the function's parameters and the table index from the top
								// of the stack.
								stack.resize(stack.size() - calleeType.params().size() - 1);

								// Push the function's results onto the stack.
								for(ValueType result : calleeType.results())
								{ stack.push_back(result); }
							});
						}
					}
				}
			}
		}

		if(allowStackGrowth)
		{
			const Uptr maxArity = stack.size() - controlStack.back().outerStackSize;
			for(Uptr arity = 0; arity < maxArity; ++arity)
			{
				// Enter a block control structure.
				validOpEmitters.push_back(
					[&stack, &controlStack, &functionTypeMap, arity](
						RandomStream& random, IR::Module& module, CodeStream& codeStream) {
						const FunctionType blockSig = generateBlockSig(
							random, TypeTuple(stack.data() + stack.size() - arity, arity));
						stack.resize(stack.size() - arity);
						stack.insert(
							stack.end(), blockSig.params().begin(), blockSig.params().end());
						codeStream.block({getIndexedBlockType(module, functionTypeMap, blockSig)});
						controlStack.push_back({ControlContext::Type::block,
												stack.size() - arity,
												blockSig.results(),
												blockSig.results(),
												TypeTuple()});
					});

				// Enter a loop control structure.
				validOpEmitters.push_back(
					[&stack, &controlStack, &functionTypeMap, arity](
						RandomStream& random, IR::Module& module, CodeStream& codeStream) {
						const FunctionType loopSig = generateBlockSig(
							random, TypeTuple(stack.data() + stack.size() - arity, arity));
						stack.resize(stack.size() - arity);
						stack.insert(stack.end(), loopSig.params().begin(), loopSig.params().end());
						codeStream.loop({getIndexedBlockType(module, functionTypeMap, loopSig)});
						controlStack.push_back({ControlContext::Type::loop,
												stack.size() - arity,
												loopSig.params(),
												loopSig.results(),
												TypeTuple()});
					});
			}
		}

		// Enter an if control structure.
		if(allowStackGrowth && stack.size() > controlStack.back().outerStackSize
		   && stack.back() == ValueType::i32)
		{
			const Uptr maxArity = stack.size() - controlStack.back().outerStackSize - 1;
			for(Uptr arity = 0; arity < maxArity; ++arity)
			{
				validOpEmitters.push_back(
					[&stack, &controlStack, &functionTypeMap, arity](
						RandomStream& random, IR::Module& module, CodeStream& codeStream) {
						const FunctionType ifSig = generateBlockSig(
							random, TypeTuple(stack.data() + stack.size() - arity - 1, arity));
						stack.resize(stack.size() - arity - 1);
						stack.insert(stack.end(), ifSig.params().begin(), ifSig.params().end());
						codeStream.if_({getIndexedBlockType(module, functionTypeMap, ifSig)});
						controlStack.push_back({ControlContext::Type::ifThen,
												stack.size() - arity,
												ifSig.results(),
												ifSig.results(),
												ifSig.params()});
					});
			}
		}

		// TODO: try/catch/catch_all

		for(Uptr branchTargetDepth = 0; branchTargetDepth < controlStack.size();
			++branchTargetDepth)
		{
			const ControlContext& targetContext
				= controlStack[controlStack.size() - branchTargetDepth - 1];
			const TypeTuple params = targetContext.params;

			if(params.size() > stack.size() - controlStack.back().outerStackSize) { continue; }

			// Check that the types of values on top of the stack are the right type for the
			// operator's parameters.
			bool sigMatch = true;
			for(Uptr paramIndex = 0; paramIndex < params.size(); ++paramIndex)
			{
				if(!isSubtype(stack[stack.size() - params.size() + paramIndex], params[paramIndex]))
				{
					sigMatch = false;
					break;
				}
			}
			if(sigMatch)
			{
				// br
				validOpEmitters.push_back(
					[&controlStack, &stack, branchTargetDepth](
						RandomStream& random, IR::Module& module, CodeStream& codeStream) {
						codeStream.br({U32(branchTargetDepth)});
						emitControlEnd(controlStack, stack, module, codeStream);
					});

				if(branchTargetDepth == controlStack.size() - 1)
				{
					// return
					validOpEmitters.push_back([&controlStack, &stack](RandomStream& random,
																	  IR::Module& module,
																	  CodeStream& codeStream) {
						codeStream.return_();
						emitControlEnd(controlStack, stack, module, codeStream);
					});
				}
			}
		}

		// br_if
		if(stack.size() > controlStack.back().outerStackSize && stack.back() == ValueType::i32)
		{
			for(Uptr branchTargetDepth = 0; branchTargetDepth < controlStack.size();
				++branchTargetDepth)
			{
				const ControlContext& targetContext
					= controlStack[controlStack.size() - branchTargetDepth - 1];
				const TypeTuple params = targetContext.params;

				if(params.size() + 1 > stack.size() - controlStack.back().outerStackSize)
				{ continue; }

				// Check that the types of values on top of the stack are the right type for the
				// operator's parameters.
				bool sigMatch = true;
				for(Uptr paramIndex = 0; paramIndex < params.size(); ++paramIndex)
				{
					if(stack[stack.size() - params.size() - 1 + paramIndex] != params[paramIndex])
					{
						sigMatch = false;
						break;
					}
				}
				if(sigMatch)
				{
					validOpEmitters.push_back([&stack, branchTargetDepth](RandomStream& random,
																		  IR::Module& module,
																		  CodeStream& codeStream) {
						stack.pop_back();
						codeStream.br_if({U32(branchTargetDepth)});
					});
				}
			}
		}

		// unreachable
		validOpEmitters.push_back([&controlStack, &stack](RandomStream& random,
														  IR::Module& module,
														  CodeStream& codeStream) {
			codeStream.unreachable();
			emitControlEnd(controlStack, stack, module, codeStream);
		});

		// TODO: br_table

		if(stack.size() - controlStack.back().outerStackSize >= 3 && stack.back() == ValueType::i32)
		{
			const ValueType trueValueType = stack[stack.size() - 3];
			const ValueType falseValueType = stack[stack.size() - 2];
			const ValueType joinType = join(trueValueType, falseValueType);
			if(joinType != ValueType::any)
			{
				// Typed select
				if(joinType == ValueType::nullref)
				{
					// If selecting between two nullrefs, both:
					//     select (result anyref)
					// and select (result funcref) are valid.
					validOpEmitters.push_back(
						[&stack](RandomStream& random, IR::Module& module, CodeStream& codeStream) {
							stack.resize(stack.size() - 3);
							stack.push_back(ValueType::anyref);
							codeStream.select({ValueType::anyref});
						});
					validOpEmitters.push_back(
						[&stack](RandomStream& random, IR::Module& module, CodeStream& codeStream) {
							stack.resize(stack.size() - 3);
							stack.push_back(ValueType::funcref);
							codeStream.select({ValueType::funcref});
						});
				}
				else
				{
					validOpEmitters.push_back([&stack, joinType](RandomStream& random,
																 IR::Module& module,
																 CodeStream& codeStream) {
						stack.resize(stack.size() - 3);
						stack.push_back(joinType);
						codeStream.select({joinType});
					});
				}
			}

			if(!isReferenceType(trueValueType) && !isReferenceType(falseValueType)
			   && trueValueType == falseValueType)
			{
				// Non-typed select
				validOpEmitters.push_back([&stack, joinType](RandomStream& random,
															 IR::Module& module,
															 CodeStream& codeStream) {
					stack.resize(stack.size() - 3);
					stack.push_back(joinType);
					codeStream.select({ValueType::any});
				});
			}
		}

		if(stack.size() > controlStack.back().outerStackSize)
		{
			// drop
			validOpEmitters.push_back(
				[&stack](RandomStream& random, IR::Module& module, CodeStream& codeStream) {
					codeStream.drop();
					stack.pop_back();
				});
		}

		// call
		for(Uptr functionIndex = 0; functionIndex < module.functions.size(); ++functionIndex)
		{
			const FunctionType calleeType
				= module.types[module.functions.getType(functionIndex).index];
			const TypeTuple params = calleeType.params();
			const TypeTuple results = calleeType.results();

			// If the random stream has run out of entropy, only consider operators that result in
			// fewer operands on the stack.
			if(!allowStackGrowth && results.size() >= params.size()) { continue; }

			// Ensure the stack has enough values for the operator's parameters.
			if(params.size() > stack.size() - controlStack.back().outerStackSize) { continue; }

			// Check that the types of values on top of the stack are the right type for the
			// operator's parameters.
			bool sigMatch = true;
			for(Uptr paramIndex = 0; paramIndex < params.size(); ++paramIndex)
			{
				if(stack[stack.size() - params.size() + paramIndex] != params[paramIndex])
				{
					sigMatch = false;
					break;
				}
			}
			if(sigMatch)
			{
				validOpEmitters.push_back([&stack, functionIndex](RandomStream& random,
																  IR::Module& module,
																  CodeStream& codeStream) {
					const FunctionType calleeType
						= module.types[module.functions.getType(functionIndex).index];

					codeStream.call({functionIndex});

					// Remove the function's parameters from the top of the stack.
					stack.resize(stack.size() - calleeType.params().size());

					// Push the function's results onto the stack.
					for(ValueType result : calleeType.results()) { stack.push_back(result); }
				});
			}
		}

		// Emit a random operator.
		wavmAssert(validOpEmitters.size());
		const Uptr randomOpIndex = random.get(validOpEmitters.size() - 1);
		validOpEmitters[randomOpIndex](random, module, codeStream);
		validOpEmitters.clear();
	};

	codeStream.finishValidation();

	functionDef.code = codeByteStream.getBytes();
};

void generateValidModule(IR::Module& module, RandomStream& random)
{
	HashMap<FunctionType, Uptr> functionTypeMap;

	// Generate some standard definitions that are the same for all modules.
	module.memories.defs.push_back({{true, {1024, IR::maxMemoryPages}}});
	module.tables.defs.push_back({{ReferenceType::funcref, true, {1024, IR::maxTableElems}}});

	// Generate some globals.
	const Uptr numGlobals = random.get(10);
	for(Uptr globalIndex = 0; globalIndex < numGlobals; ++globalIndex)
	{
		const ValueType globalValueType = generateValueType(random);

		const bool isMutable = random.get(1);
		const GlobalType globalType{globalValueType, isMutable};
		if(random.get(1))
		{
			module.imports.push_back({ExternKind::global, module.globals.imports.size()});
			module.globals.imports.push_back(
				{globalType, "env", "global" + std::to_string(globalIndex)});
		}
		else
		{
			InitializerExpression initializer;
			switch(globalValueType)
			{
			case ValueType::i32:
				initializer.type = InitializerExpression::Type::i32_const;
				initializer.i32 = I32(random.get(UINT32_MAX));
				break;
			case ValueType::i64:
				initializer.type = InitializerExpression::Type::i64_const;
				initializer.i64 = I64(random.get(UINT64_MAX));
				break;
			case ValueType::f32:
				initializer.type = InitializerExpression::Type::f32_const;
				initializer.f32 = F32(random.get(UINT32_MAX));
				break;
			case ValueType::f64:
				initializer.type = InitializerExpression::Type::f64_const;
				initializer.f64 = F64(random.get(UINT64_MAX));
				break;
			case ValueType::v128:
				initializer.type = InitializerExpression::Type::v128_const;
				initializer.v128.u64[0] = random.get(UINT64_MAX);
				initializer.v128.u64[1] = random.get(UINT64_MAX);
				break;
			case ValueType::anyref:
			case ValueType::funcref:
				initializer.type = InitializerExpression::Type::ref_null;
				break;

			case ValueType::none:
			case ValueType::any:
			case ValueType::nullref:
			default: WAVM_UNREACHABLE();
			}
			module.globals.defs.push_back({globalType, initializer});
		}
	};

	// Generate some data segments.
	Uptr numDataSegments = 1 + random.get(4);
	for(Uptr segmentIndex = 0; segmentIndex < numDataSegments; ++segmentIndex)
	{
		const Uptr numSegmentBytes = 1 + random.get(100);
		std::vector<U8> bytes;
		for(Uptr byteIndex = 0; byteIndex < numSegmentBytes; ++byteIndex)
		{ bytes.push_back(random.get<U8>(255)); }
		module.dataSegments.push_back(
			{false, UINTPTR_MAX, {}, std::make_shared<std::vector<U8>>(std::move(bytes))});
	};

	// Create FunctionDefs for all the function we will generate, but don't yet generate their code.
	const Uptr numFunctionDefs = 3;
	while(module.functions.defs.size() < numFunctionDefs)
	{
		// Generate a signature.
		std::vector<ValueType> functionParams;
		const Uptr numParams = random.get(4);
		for(Uptr paramIndex = 0; paramIndex < numParams; ++paramIndex)
		{ functionParams.push_back(generateValueType(random)); };

		// const ValueType resultType = ValueType(random.get(U32(maxValueType)));
		// FunctionType functionType(resultType == ValueType::any ? TypeTuple() :
		// TypeTuple{resultType},
		//						  TypeTuple(functionParams));
		FunctionType functionType({}, TypeTuple(functionParams));

		FunctionDef functionDef;
		functionDef.type.index = functionTypeMap.getOrAdd(functionType, module.types.size());
		if(functionDef.type.index == module.types.size()) { module.types.push_back(functionType); }
		module.functions.defs.push_back(std::move(functionDef));
	};

	// Generate some elem segments.
	Uptr numElemSegments = 1 + random.get(4);
	for(Uptr segmentIndex = 0; segmentIndex < numElemSegments; ++segmentIndex)
	{
		const Uptr numSegmentElements = 1 + random.get(100);
		std::vector<Elem> elems;
		for(Uptr index = 0; index < numSegmentElements; ++index)
		{
			const Uptr functionIndex = random.get(numFunctionDefs);
			if(functionIndex == numFunctionDefs) { elems.push_back({{Elem::Type::ref_null}}); }
			else
			{
				elems.push_back({{Elem::Type::ref_func}, functionIndex});
			}
		}
		module.elemSegments.push_back({false,
									   UINTPTR_MAX,
									   {},
									   ReferenceType::funcref,
									   std::make_shared<std::vector<Elem>>(std::move(elems))});
	};

	validatePreCodeSections(module);

	// Generate a few functions.
	for(FunctionDef& functionDef : module.functions.defs)
	{ generateFunction(random, module, functionDef, functionTypeMap); };

	// Generating functions might have added some block types, so revalidate the type section.
	validateTypes(module);

	validatePostCodeSections(module);
}

static void compileModule(const IR::Module& module, RandomStream& random)
{
	static const LLVMJIT::TargetSpec possibleTargetSpecs[] = {
		LLVMJIT::TargetSpec{"x86_64-pc-windows-msvc", "skylake-avx512"},
		LLVMJIT::TargetSpec{"x86_64-apple-darwin18.6.0", "skylake-avx512"},
		LLVMJIT::TargetSpec{"x86_64-unknown-linux-gnu", "skylake-avx512"},
		LLVMJIT::TargetSpec{"x86_64-unknown-linux-gnu", "skylake"},
		LLVMJIT::TargetSpec{"x86_64-unknown-linux-gnu", "core2"},
	};
	static const Uptr numPossibleTargets
		= sizeof(possibleTargetSpecs) / sizeof(LLVMJIT::TargetSpec);

	const LLVMJIT::TargetSpec& targetSpec = possibleTargetSpecs[random.get(numPossibleTargets - 1)];

	std::vector<U8> objectCode;
	errorUnless(LLVMJIT::compileModule(module, targetSpec, objectCode)
				== LLVMJIT::CompileResult::success);
}

extern "C" I32 LLVMFuzzerTestOneInput(const U8* data, Uptr numBytes)
{
	RandomStream random(data, numBytes);

	IR::Module module;
	generateValidModule(module, random);
	compileModule(module, random);

	return 0;
}

#if !WAVM_ENABLE_LIBFUZZER

#include "WAVM/Inline/CLI.h"
#include "WAVM/WASTPrint/WASTPrint.h"

I32 main(int argc, char** argv)
{
	if(argc != 2)
	{
		Log::printf(Log::error, "Usage: FuzzCompileModel in.wasm\n");
		return EXIT_FAILURE;
	}
	const char* inputFilename = argv[1];

	std::vector<U8> inputBytes;
	if(!loadFile(inputFilename, inputBytes)) { return EXIT_FAILURE; }

	RandomStream random(inputBytes.data(), inputBytes.size());

	IR::Module module;
	generateValidModule(module, random);

	std::string wastString = WAST::print(module);
	Log::printf(Log::Category::debug, "Generated module WAST:\n%s\n", wastString.c_str());

	compileModule(module, random);

	return EXIT_SUCCESS;
}
#endif
