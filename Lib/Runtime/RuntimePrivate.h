#pragma once

#include "WAVM/IR/Module.h"
#include "WAVM/Inline/BasicTypes.h"
#include "WAVM/Inline/DenseStaticIntSet.h"
#include "WAVM/Inline/HashMap.h"
#include "WAVM/Inline/HashSet.h"
#include "WAVM/Inline/IndexMap.h"
#include "WAVM/LLVMJIT/LLVMJIT.h"
#include "WAVM/Platform/Defines.h"
#include "WAVM/Platform/Mutex.h"
#include "WAVM/Runtime/Intrinsics.h"
#include "WAVM/Runtime/Runtime.h"
#include "WAVM/Runtime/RuntimeData.h"

#include <atomic>
#include <functional>
#include <memory>

namespace WAVM { namespace Intrinsics {
	struct Module;
}}

namespace WAVM { namespace Runtime {

	// A private base class for all runtime objects that are garbage collected.
	struct GCObject : Object
	{
		Compartment* const compartment;
		mutable std::atomic<Uptr> numRootReferences{0};
		void* userData{nullptr};
		void (*finalizeUserData)(void*);

		GCObject(ObjectKind inKind, Compartment* inCompartment);
		virtual ~GCObject();
	};

	// An instance of a WebAssembly Table.
	struct Table : GCObject
	{
		struct Element
		{
			std::atomic<Uptr> biasedValue;
		};

		Uptr id = UINTPTR_MAX;
		const IR::TableType type;
		std::string debugName;

		Element* elements = nullptr;
		Uptr numReservedBytes = 0;
		Uptr numReservedElements = 0;

		mutable Platform::Mutex resizingMutex;
		std::atomic<Uptr> numElements{0};

		Table(Compartment* inCompartment, const IR::TableType& inType, std::string&& inDebugName)
		: GCObject(ObjectKind::table, inCompartment)
		, type(inType)
		, debugName(std::move(inDebugName))
		{
		}
		~Table() override;
	};

	// This is used as a sentinel value for table elements that are out-of-bounds. The address of
	// this Object is subtracted from every address stored in the table, so zero-initialized pages
	// at the end of the array will, when re-adding this Function's address, point to this Object.
	extern Object* getOutOfBoundsElement();

	// An instance of a WebAssembly Memory.
	struct Memory : GCObject
	{
		Uptr id = UINTPTR_MAX;
		IR::MemoryType type;
		std::string debugName;

		U8* baseAddress = nullptr;
		Uptr numReservedBytes = 0;

		mutable Platform::Mutex resizingMutex;
		std::atomic<Uptr> numPages{0};

		Memory(Compartment* inCompartment, const IR::MemoryType& inType, std::string&& inDebugName)
		: GCObject(ObjectKind::memory, inCompartment)
		, type(inType)
		, debugName(std::move(inDebugName))
		{
		}
		~Memory() override;
	};

	// An instance of a WebAssembly global.
	struct Global : GCObject
	{
		Uptr id = UINTPTR_MAX;

		const IR::GlobalType type;
		const U32 mutableGlobalIndex;
		IR::UntaggedValue initialValue;
		bool hasBeenInitialized;

		Global(Compartment* inCompartment,
			   IR::GlobalType inType,
			   U32 inMutableGlobalId,
			   IR::UntaggedValue inInitialValue = IR::UntaggedValue())
		: GCObject(ObjectKind::global, inCompartment)
		, type(inType)
		, mutableGlobalIndex(inMutableGlobalId)
		, initialValue(inInitialValue)
		, hasBeenInitialized(false)
		{
		}
		~Global() override;
	};

	// An instance of a WebAssembly exception type.
	struct ExceptionType : GCObject
	{
		Uptr id = UINTPTR_MAX;

		IR::ExceptionType sig;
		std::string debugName;

		ExceptionType(Compartment* inCompartment,
					  IR::ExceptionType inSig,
					  std::string&& inDebugName)
		: GCObject(ObjectKind::exceptionType, inCompartment)
		, sig(inSig)
		, debugName(std::move(inDebugName))
		{
		}

		~ExceptionType() override;
	};

	typedef std::vector<std::shared_ptr<std::vector<U8>>> DataSegmentVector;
	typedef std::vector<std::shared_ptr<std::vector<IR::Elem>>> ElemSegmentVector;

	// A compiled WebAssembly module.
	struct Module
	{
		IR::Module ir;
		std::vector<U8> objectCode;

		Module(IR::Module&& inIR, std::vector<U8>&& inObjectCode)
		: ir(inIR), objectCode(std::move(inObjectCode))
		{
		}
	};

	// An instance of a WebAssembly module.
	struct ModuleInstance : GCObject
	{
		const Uptr id;
		const std::string debugName;

		const HashMap<std::string, Object*> exportMap;
		const std::vector<Object*> exports;

		const std::vector<Function*> functions;
		const std::vector<Table*> tables;
		const std::vector<Memory*> memories;
		const std::vector<Global*> globals;
		const std::vector<ExceptionType*> exceptionTypes;

		Function* const startFunction;

		mutable Platform::Mutex dataSegmentsMutex;
		DataSegmentVector dataSegments;

		mutable Platform::Mutex elemSegmentsMutex;
		ElemSegmentVector elemSegments;

		const std::shared_ptr<LLVMJIT::Module> jitModule;

		ModuleInstance(Compartment* inCompartment,
					   Uptr inID,
					   HashMap<std::string, Object*>&& inExportMap,
					   std::vector<Object*>&& inExports,
					   std::vector<Function*>&& inFunctions,
					   std::vector<Table*>&& inTables,
					   std::vector<Memory*>&& inMemories,
					   std::vector<Global*>&& inGlobals,
					   std::vector<ExceptionType*>&& inExceptionTypes,
					   Function* inStartFunction,
					   DataSegmentVector&& inPassiveDataSegments,
					   ElemSegmentVector&& inPassiveElemSegments,
					   std::shared_ptr<LLVMJIT::Module>&& inJITModule,
					   std::string&& inDebugName)
		: GCObject(ObjectKind::moduleInstance, inCompartment)
		, id(inID)
		, debugName(std::move(inDebugName))
		, exportMap(std::move(inExportMap))
		, exports(std::move(inExports))
		, functions(std::move(inFunctions))
		, tables(std::move(inTables))
		, memories(std::move(inMemories))
		, globals(std::move(inGlobals))
		, exceptionTypes(std::move(inExceptionTypes))
		, startFunction(inStartFunction)
		, dataSegments(std::move(inPassiveDataSegments))
		, elemSegments(std::move(inPassiveElemSegments))
		, jitModule(std::move(inJITModule))
		{
		}

		virtual ~ModuleInstance() override;
	};

	struct Context : GCObject
	{
		Uptr id = UINTPTR_MAX;
		struct ContextRuntimeData* runtimeData = nullptr;

		Context(Compartment* inCompartment) : GCObject(ObjectKind::context, inCompartment) {}
		~Context();
	};

	struct Compartment : GCObject
	{
		mutable Platform::Mutex mutex;

		struct CompartmentRuntimeData* runtimeData;
		U8* unalignedRuntimeData;

		IndexMap<Uptr, Table*> tables;
		IndexMap<Uptr, Memory*> memories;
		IndexMap<Uptr, Global*> globals;
		IndexMap<Uptr, ExceptionType*> exceptionTypes;
		IndexMap<Uptr, ModuleInstance*> moduleInstances;
		IndexMap<Uptr, Context*> contexts;

		DenseStaticIntSet<U32, maxMutableGlobals> globalDataAllocationMask;
		IR::UntaggedValue initialContextMutableGlobals[maxMutableGlobals];

		Compartment();
		~Compartment();
	};

	struct Foreign : GCObject
	{
		Foreign(Compartment* inCompartment) : GCObject(ObjectKind::foreign, inCompartment) {}
	};

	DECLARE_INTRINSIC_MODULE(wavmIntrinsics);
	DECLARE_INTRINSIC_MODULE(wavmIntrinsicsAtomics);
	DECLARE_INTRINSIC_MODULE(wavmIntrinsicsException);
	DECLARE_INTRINSIC_MODULE(wavmIntrinsicsMemory);
	DECLARE_INTRINSIC_MODULE(wavmIntrinsicsTable);

	// Checks whether an address is owned by a table or memory.
	bool isAddressOwnedByTable(U8* address, Table*& outTable, Uptr& outTableIndex);
	bool isAddressOwnedByMemory(U8* address, Memory*& outMemory, Uptr& outMemoryAddress);

	// Clones objects into a new compartment with the same ID.
	Table* cloneTable(Table* memory, Compartment* newCompartment);
	Memory* cloneMemory(Memory* memory, Compartment* newCompartment);
	ExceptionType* cloneExceptionType(ExceptionType* exceptionType, Compartment* newCompartment);
	ModuleInstance* cloneModuleInstance(ModuleInstance* moduleInstance,
										Compartment* newCompartment);

	// Clone a global with same ID and mutable data offset (if mutable) in a new compartment.
	Global* cloneGlobal(Global* global, Compartment* newCompartment);

	ModuleInstance* getModuleInstanceFromRuntimeData(ContextRuntimeData* contextRuntimeData,
													 Uptr moduleInstanceId);
	Table* getTableFromRuntimeData(ContextRuntimeData* contextRuntimeData, Uptr tableId);
	Memory* getMemoryFromRuntimeData(ContextRuntimeData* contextRuntimeData, Uptr memoryId);

	// Initialize a data segment (equivalent to executing a memory.init instruction).
	void initDataSegment(ModuleInstance* moduleInstance,
						 Uptr dataSegmentIndex,
						 const std::vector<U8>* dataVector,
						 Memory* memory,
						 Uptr destAddress,
						 Uptr sourceOffset,
						 Uptr numBytes);

	// Initialize a table segment (equivalent to executing a table.init instruction).
	void initElemSegment(ModuleInstance* moduleInstance,
						 Uptr elemSegmentIndex,
						 const std::vector<IR::Elem>* elemVector,
						 Table* table,
						 Uptr destOffset,
						 Uptr sourceOffset,
						 Uptr numElems);
}}

namespace WAVM { namespace Intrinsics {
	HashMap<std::string, Function*> getUninstantiatedFunctions(
		const std::initializer_list<const Intrinsics::Module*>& moduleRefs);
}}
