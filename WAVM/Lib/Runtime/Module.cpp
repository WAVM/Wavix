#include <string.h>
#include <atomic>
#include <memory>
#include <utility>

#include "RuntimePrivate.h"
#include "WAVM/IR/IR.h"
#include "WAVM/IR/Module.h"
#include "WAVM/IR/Types.h"
#include "WAVM/IR/Value.h"
#include "WAVM/Inline/Assert.h"
#include "WAVM/Inline/BasicTypes.h"
#include "WAVM/Inline/Hash.h"
#include "WAVM/Inline/HashMap.h"
#include "WAVM/Inline/Lock.h"
#include "WAVM/Inline/Serialization.h"
#include "WAVM/LLVMJIT/LLVMJIT.h"
#include "WAVM/Platform/Intrinsic.h"
#include "WAVM/Platform/Mutex.h"
#include "WAVM/Runtime/Runtime.h"

using namespace WAVM;
using namespace WAVM::IR;
using namespace WAVM::Runtime;

static Value evaluateInitializer(const std::vector<Global*>& moduleGlobals,
								 InitializerExpression expression)
{
	switch(expression.type)
	{
	case InitializerExpression::Type::i32_const: return expression.i32;
	case InitializerExpression::Type::i64_const: return expression.i64;
	case InitializerExpression::Type::f32_const: return expression.f32;
	case InitializerExpression::Type::f64_const: return expression.f64;
	case InitializerExpression::Type::v128_const: return expression.v128;
	case InitializerExpression::Type::global_get:
	{
		// Find the import this refers to.
		errorUnless(expression.ref < moduleGlobals.size());
		Global* global = moduleGlobals[expression.ref];
		errorUnless(global);
		errorUnless(!global->type.isMutable);
		return IR::Value(global->type.valueType, global->initialValue);
	}
	case InitializerExpression::Type::ref_null: return nullptr;

	case InitializerExpression::Type::ref_func:
		// instantiateModule delays evaluating ref.func initializers until the module is loaded and
		// we have addresses for its functions.

	case InitializerExpression::Type::invalid:
	default: WAVM_UNREACHABLE();
	};
}

ModuleRef Runtime::compileModule(const IR::Module& irModule)
{
	std::vector<U8> objectCode = LLVMJIT::compileModule(irModule);
	return std::make_shared<Module>(IR::Module(irModule), std::move(objectCode));
}

std::vector<U8> Runtime::getObjectCode(ModuleConstRefParam module) { return module->objectCode; }

ModuleRef Runtime::loadPrecompiledModule(const IR::Module& irModule,
										 const std::vector<U8>& objectCode)
{
	return std::make_shared<Module>(IR::Module(irModule), std::vector<U8>(objectCode));
}

const IR::Module& Runtime::getModuleIR(ModuleConstRefParam module) { return module->ir; }

ModuleInstance::~ModuleInstance()
{
	if(id != UINTPTR_MAX)
	{
		wavmAssertMutexIsLockedByCurrentThread(compartment->mutex);
		compartment->moduleInstances.removeOrFail(id);
	}
}

ModuleInstance* Runtime::instantiateModule(Compartment* compartment,
										   ModuleConstRefParam module,
										   ImportBindings&& imports,
										   std::string&& moduleDebugName,
										   ResourceQuotaRefParam resourceQuota)
{
	Uptr id = UINTPTR_MAX;
	{
		Lock<Platform::Mutex> compartmentLock(compartment->mutex);
		id = compartment->moduleInstances.add(UINTPTR_MAX, nullptr);
	}
	if(id == UINTPTR_MAX) { return nullptr; }

	std::vector<Function*> functions;
	std::vector<Table*> tables;
	std::vector<Memory*> memories;
	std::vector<Global*> globals;
	std::vector<ExceptionType*> exceptionTypes;

	// Check the types of the ModuleInstance's imports.
	errorUnless(imports.size() == module->ir.imports.size());
	for(Uptr importIndex = 0; importIndex < imports.size(); ++importIndex)
	{
		const auto& kindIndex = module->ir.imports[importIndex];
		Object* importObject = imports[importIndex];

		errorUnless(isInCompartment(importObject, compartment));
		errorUnless(importObject->kind == ObjectKind(kindIndex.kind));

		switch(kindIndex.kind)
		{
		case ExternKind::function:
		{
			Function* function = asFunction(importObject);
			const auto& importType
				= module->ir.types[module->ir.functions.getType(kindIndex.index).index];
			errorUnless(function->encodedType == importType);
			functions.push_back(function);
			break;
		}
		case ExternKind::table:
		{
			Table* table = asTable(importObject);
			errorUnless(isSubtype(table->type, module->ir.tables.getType(kindIndex.index)));
			tables.push_back(table);
			break;
		}
		case ExternKind::memory:
		{
			Memory* memory = asMemory(importObject);
			errorUnless(isSubtype(memory->type, module->ir.memories.getType(kindIndex.index)));
			memories.push_back(memory);
			break;
		}
		case ExternKind::global:
		{
			Global* global = asGlobal(importObject);
			errorUnless(isSubtype(global->type, module->ir.globals.getType(kindIndex.index)));
			globals.push_back(global);
			break;
		}
		case ExternKind::exceptionType:
		{
			ExceptionType* exceptionType = asExceptionType(importObject);
			errorUnless(isSubtype(exceptionType->sig.params,
								  module->ir.exceptionTypes.getType(kindIndex.index).params));
			exceptionTypes.push_back(exceptionType);
			break;
		}

		case ExternKind::invalid:
		default: WAVM_UNREACHABLE();
		};
	}

	wavmAssert(functions.size() == module->ir.functions.imports.size());
	wavmAssert(tables.size() == module->ir.tables.imports.size());
	wavmAssert(memories.size() == module->ir.memories.imports.size());
	wavmAssert(globals.size() == module->ir.globals.imports.size());
	wavmAssert(exceptionTypes.size() == module->ir.exceptionTypes.imports.size());

	// Deserialize the disassembly names.
	DisassemblyNames disassemblyNames;
	getDisassemblyNames(module->ir, disassemblyNames);

	// Instantiate the module's memory and table definitions.
	for(Uptr tableDefIndex = 0; tableDefIndex < module->ir.tables.defs.size(); ++tableDefIndex)
	{
		std::string debugName
			= disassemblyNames.tables[module->ir.tables.imports.size() + tableDefIndex];
		auto table = createTable(compartment,
								 module->ir.tables.defs[tableDefIndex].type,
								 nullptr,
								 std::move(debugName),
								 resourceQuota);
		if(!table)
		{
			Lock<Platform::Mutex> compartmentLock(compartment->mutex);
			compartment->moduleInstances.removeOrFail(id);
			throwException(ExceptionTypes::outOfMemory);
		}
		tables.push_back(table);
	}
	for(Uptr memoryDefIndex = 0; memoryDefIndex < module->ir.memories.defs.size(); ++memoryDefIndex)
	{
		std::string debugName
			= disassemblyNames.memories[module->ir.memories.imports.size() + memoryDefIndex];
		auto memory = createMemory(compartment,
								   module->ir.memories.defs[memoryDefIndex].type,
								   std::move(debugName),
								   resourceQuota);
		if(!memory)
		{
			Lock<Platform::Mutex> compartmentLock(compartment->mutex);
			compartment->moduleInstances.removeOrFail(id);
			throwException(ExceptionTypes::outOfMemory);
		}
		memories.push_back(memory);
	}

	// Instantiate the module's global definitions.
	for(const GlobalDef& globalDef : module->ir.globals.defs)
	{
		Global* global = createGlobal(compartment, globalDef.type, resourceQuota);
		globals.push_back(global);

		// Defer evaluation of globals with (ref.func ...) initializers until the module's code is
		// loaded and we have pointers to the Runtime::Function objects.
		if(globalDef.initializer.type != InitializerExpression::Type::ref_func)
		{
			const Value initialValue = evaluateInitializer(globals, globalDef.initializer);
			errorUnless(isSubtype(initialValue.type, globalDef.type.valueType));
			initializeGlobal(global, initialValue);
		}
	}

	// Instantiate the module's exception types.
	for(Uptr exceptionTypeDefIndex = 0;
		exceptionTypeDefIndex < module->ir.exceptionTypes.defs.size();
		++exceptionTypeDefIndex)
	{
		const ExceptionTypeDef& exceptionTypeDef
			= module->ir.exceptionTypes.defs[exceptionTypeDefIndex];
		std::string debugName
			= disassemblyNames
				  .exceptionTypes[module->ir.exceptionTypes.imports.size() + exceptionTypeDefIndex];
		exceptionTypes.push_back(
			createExceptionType(compartment, exceptionTypeDef.type, std::move(debugName)));
	}

	// Set up the values to bind to the symbols in the LLVMJIT object code.
	HashMap<std::string, LLVMJIT::FunctionBinding> wavmIntrinsicsExportMap;
	for(const HashMapPair<std::string, Intrinsics::Function*>& intrinsicFunctionPair :
		Intrinsics::getUninstantiatedFunctions({WAVM_INTRINSIC_MODULE_REF(wavmIntrinsics),
												WAVM_INTRINSIC_MODULE_REF(wavmIntrinsicsAtomics),
												WAVM_INTRINSIC_MODULE_REF(wavmIntrinsicsException),
												WAVM_INTRINSIC_MODULE_REF(wavmIntrinsicsMemory),
												WAVM_INTRINSIC_MODULE_REF(wavmIntrinsicsTable)}))
	{
		LLVMJIT::FunctionBinding functionBinding{
			intrinsicFunctionPair.value->getCallingConvention(),
			intrinsicFunctionPair.value->getNativeFunction()};
		wavmIntrinsicsExportMap.add(intrinsicFunctionPair.key, functionBinding);
	}

	std::vector<LLVMJIT::FunctionBinding> jitFunctionImports;
	for(Uptr importIndex = 0; importIndex < module->ir.functions.imports.size(); ++importIndex)
	{
		jitFunctionImports.push_back(
			{CallingConvention::wasm, const_cast<U8*>(functions[importIndex]->code)});
	}

	std::vector<LLVMJIT::TableBinding> jitTables;
	for(Table* table : tables) { jitTables.push_back({table->id}); }

	std::vector<LLVMJIT::MemoryBinding> jitMemories;
	for(Memory* memory : memories) { jitMemories.push_back({memory->id}); }

	std::vector<LLVMJIT::GlobalBinding> jitGlobals;
	for(Global* global : globals)
	{
		LLVMJIT::GlobalBinding globalSpec;
		globalSpec.type = global->type;
		if(global->type.isMutable) { globalSpec.mutableGlobalIndex = global->mutableGlobalIndex; }
		else
		{
			globalSpec.immutableValuePointer = &global->initialValue;
		}
		jitGlobals.push_back(globalSpec);
	}

	std::vector<LLVMJIT::ExceptionTypeBinding> jitExceptionTypes;
	for(ExceptionType* exceptionType : exceptionTypes)
	{ jitExceptionTypes.push_back({exceptionType->id}); }

	// Create a FunctionMutableData for each function definition.
	std::vector<FunctionMutableData*> functionDefMutableDatas;
	for(Uptr functionDefIndex = 0; functionDefIndex < module->ir.functions.defs.size();
		++functionDefIndex)
	{
		std::string debugName
			= disassemblyNames.functions[module->ir.functions.imports.size() + functionDefIndex]
				  .name;
		if(!debugName.size())
		{ debugName = "<function #" + std::to_string(functionDefIndex) + ">"; }
		debugName = "wasm!" + moduleDebugName + '!' + debugName;

		functionDefMutableDatas.push_back(new FunctionMutableData(std::move(debugName)));
	}

	// Load the compiled module's object code with this module instance's imports.
	std::vector<FunctionType> jitTypes = module->ir.types;
	std::vector<Runtime::Function*> jitFunctionDefs;
	jitFunctionDefs.resize(module->ir.functions.defs.size(), nullptr);
	std::shared_ptr<LLVMJIT::Module> jitModule
		= LLVMJIT::loadModule(module->objectCode,
							  std::move(wavmIntrinsicsExportMap),
							  std::move(jitTypes),
							  std::move(jitFunctionImports),
							  std::move(jitTables),
							  std::move(jitMemories),
							  std::move(jitGlobals),
							  std::move(jitExceptionTypes),
							  {id},
							  reinterpret_cast<Uptr>(getOutOfBoundsElement()),
							  functionDefMutableDatas);

	// LLVMJIT::loadModule filled in the functionDefMutableDatas' function pointers with the
	// compiled functions. Add those functions to the module.
	for(FunctionMutableData* functionMutableData : functionDefMutableDatas)
	{ functions.push_back(functionMutableData->function); }

	// Set up the instance's exports.
	HashMap<std::string, Object*> exportMap;
	std::vector<Object*> exports;
	for(const Export& exportIt : module->ir.exports)
	{
		Object* exportedObject = nullptr;
		switch(exportIt.kind)
		{
		case IR::ExternKind::function: exportedObject = asObject(functions[exportIt.index]); break;
		case IR::ExternKind::table: exportedObject = tables[exportIt.index]; break;
		case IR::ExternKind::memory: exportedObject = memories[exportIt.index]; break;
		case IR::ExternKind::global: exportedObject = globals[exportIt.index]; break;
		case IR::ExternKind::exceptionType: exportedObject = exceptionTypes[exportIt.index]; break;

		case IR::ExternKind::invalid:
		default: WAVM_UNREACHABLE();
		}
		exportMap.addOrFail(exportIt.name, exportedObject);
		exports.push_back(exportedObject);
	}

	// Copy the module's data and elem segments into the ModuleInstance for later use.
	DataSegmentVector dataSegments;
	ElemSegmentVector elemSegments;
	for(const DataSegment& dataSegment : module->ir.dataSegments)
	{ dataSegments.push_back(dataSegment.isActive ? nullptr : dataSegment.data); }
	for(const ElemSegment& elemSegment : module->ir.elemSegments)
	{ elemSegments.push_back(elemSegment.isActive ? nullptr : elemSegment.elems); }

	// Look up the module's start function.
	Function* startFunction = nullptr;
	if(module->ir.startFunctionIndex != UINTPTR_MAX)
	{
		startFunction = functions[module->ir.startFunctionIndex];
		wavmAssert(FunctionType(startFunction->encodedType) == FunctionType());
	}

	// Create the ModuleInstance and add it to the compartment's modules list.
	ModuleInstance* moduleInstance = new ModuleInstance(compartment,
														id,
														std::move(exportMap),
														std::move(exports),
														std::move(functions),
														std::move(tables),
														std::move(memories),
														std::move(globals),
														std::move(exceptionTypes),
														startFunction,
														std::move(dataSegments),
														std::move(elemSegments),
														std::move(jitModule),
														std::move(moduleDebugName),
														resourceQuota);
	{
		Lock<Platform::Mutex> compartmentLock(compartment->mutex);
		compartment->moduleInstances[id] = moduleInstance;
	}

	// Initialize the globals with (ref.func ...) initializers that were deferred until after the
	// Runtime::Function objects were loaded.
	for(Uptr globalDefIndex = 0; globalDefIndex < module->ir.globals.defs.size(); ++globalDefIndex)
	{
		const GlobalDef& globalDef = module->ir.globals.defs[globalDefIndex];
		if(globalDef.initializer.type == InitializerExpression::Type::ref_func)
		{
			Global* global
				= moduleInstance->globals[module->ir.globals.imports.size() + globalDefIndex];
			initializeGlobal(global, moduleInstance->functions[globalDef.initializer.ref]);
		}
	}

	// Copy the module's data segments into their designated memory instances.
	for(Uptr segmentIndex = 0; segmentIndex < module->ir.dataSegments.size(); ++segmentIndex)
	{
		const DataSegment& dataSegment = module->ir.dataSegments[segmentIndex];
		if(dataSegment.isActive)
		{
			wavmAssert(moduleInstance->dataSegments[segmentIndex] == nullptr);

			const Value baseOffsetValue
				= evaluateInitializer(moduleInstance->globals, dataSegment.baseOffset);
			errorUnless(baseOffsetValue.type == ValueType::i32);
			const U32 baseOffset = baseOffsetValue.i32;

			initDataSegment(moduleInstance,
							segmentIndex,
							dataSegment.data.get(),
							moduleInstance->memories[dataSegment.memoryIndex],
							baseOffset,
							0,
							dataSegment.data->size());
		}
	}

	// Copy the module's elem segments into their designated table instances.
	for(Uptr segmentIndex = 0; segmentIndex < module->ir.elemSegments.size(); ++segmentIndex)
	{
		const ElemSegment& elemSegment = module->ir.elemSegments[segmentIndex];
		if(elemSegment.isActive)
		{
			wavmAssert(moduleInstance->elemSegments[segmentIndex] == nullptr);

			const Value baseOffsetValue
				= evaluateInitializer(moduleInstance->globals, elemSegment.baseOffset);
			errorUnless(baseOffsetValue.type == ValueType::i32);
			const U32 baseOffset = baseOffsetValue.i32;

			Table* table = moduleInstance->tables[elemSegment.tableIndex];
			initElemSegment(moduleInstance,
							segmentIndex,
							elemSegment.elems.get(),
							table,
							baseOffset,
							0,
							elemSegment.elems->size());
		}
	}

	return moduleInstance;
}

ModuleInstance* Runtime::cloneModuleInstance(ModuleInstance* moduleInstance,
											 Compartment* newCompartment)
{
	// Remap the module's references to the cloned compartment.
	HashMap<std::string, Object*> newExportMap;
	for(const auto& pair : moduleInstance->exportMap)
	{ newExportMap.add(pair.key, remapToClonedCompartment(pair.value, newCompartment)); }
	std::vector<Object*> newExports;
	for(Object* exportObject : moduleInstance->exports)
	{ newExports.push_back(remapToClonedCompartment(exportObject, newCompartment)); }

	std::vector<Function*> newFunctions = moduleInstance->functions;

	std::vector<Table*> newTables;
	for(Table* table : moduleInstance->tables)
	{ newTables.push_back(remapToClonedCompartment(table, newCompartment)); }

	std::vector<Memory*> newMemories;
	for(Memory* memory : moduleInstance->memories)
	{ newMemories.push_back(remapToClonedCompartment(memory, newCompartment)); }

	std::vector<Global*> newGlobals;
	for(Global* global : moduleInstance->globals)
	{ newGlobals.push_back(remapToClonedCompartment(global, newCompartment)); }

	std::vector<ExceptionType*> newExceptionTypes;
	for(ExceptionType* exceptionType : moduleInstance->exceptionTypes)
	{ newExceptionTypes.push_back(remapToClonedCompartment(exceptionType, newCompartment)); }

	Function* newStartFunction
		= remapToClonedCompartment(moduleInstance->startFunction, newCompartment);

	DataSegmentVector newDataSegments;
	{
		Lock<Platform::Mutex> passiveDataSegmentsLock(moduleInstance->dataSegmentsMutex);
		newDataSegments = moduleInstance->dataSegments;
	}

	ElemSegmentVector newElemSegments;
	{
		Lock<Platform::Mutex> passiveElemSegmentsLock(moduleInstance->elemSegmentsMutex);
		newElemSegments = moduleInstance->elemSegments;
	}

	// Create the new ModuleInstance in the cloned compartment, but with the same ID as the old one.
	std::shared_ptr<LLVMJIT::Module> jitModuleCopy = moduleInstance->jitModule;
	ModuleInstance* newModuleInstance = new ModuleInstance(newCompartment,
														   moduleInstance->id,
														   std::move(newExportMap),
														   std::move(newExports),
														   std::move(newFunctions),
														   std::move(newTables),
														   std::move(newMemories),
														   std::move(newGlobals),
														   std::move(newExceptionTypes),
														   std::move(newStartFunction),
														   std::move(newDataSegments),
														   std::move(newElemSegments),
														   std::move(jitModuleCopy),
														   std::string(moduleInstance->debugName),
														   moduleInstance->resourceQuota);
	{
		Lock<Platform::Mutex> compartmentLock(newCompartment->mutex);
		newCompartment->moduleInstances.insertOrFail(moduleInstance->id, newModuleInstance);
	}

	return newModuleInstance;
}

Function* Runtime::getStartFunction(const ModuleInstance* moduleInstance)
{
	return moduleInstance->startFunction;
}

Memory* Runtime::getDefaultMemory(const ModuleInstance* moduleInstance)
{
	return moduleInstance->memories.size() ? moduleInstance->memories[0] : nullptr;
}
Table* Runtime::getDefaultTable(const ModuleInstance* moduleInstance)
{
	return moduleInstance->tables.size() ? moduleInstance->tables[0] : nullptr;
}

Object* Runtime::getInstanceExport(const ModuleInstance* moduleInstance, const std::string& name)
{
	wavmAssert(moduleInstance);
	Object* const* exportedObjectPtr = moduleInstance->exportMap.get(name);
	return exportedObjectPtr ? *exportedObjectPtr : nullptr;
}

const std::vector<Object*>& Runtime::getInstanceExports(const ModuleInstance* moduleInstance)
{
	return moduleInstance->exports;
}
