#include <stdint.h>
#include <string.h>
#include <atomic>
#include <memory>
#include <vector>

#include "RuntimePrivate.h"
#include "WAVM/IR/IR.h"
#include "WAVM/IR/Types.h"
#include "WAVM/IR/Value.h"
#include "WAVM/Inline/Assert.h"
#include "WAVM/Inline/BasicTypes.h"
#include "WAVM/Inline/Lock.h"
#include "WAVM/Platform/Intrinsic.h"
#include "WAVM/Platform/Memory.h"
#include "WAVM/Platform/Mutex.h"
#include "WAVM/Runtime/Runtime.h"
#include "WAVM/Runtime/RuntimeData.h"

using namespace WAVM;
using namespace WAVM::Runtime;

namespace WAVM { namespace Runtime {
	WAVM_DEFINE_INTRINSIC_MODULE(wavmIntrinsicsMemory)
}}

// Global lists of memories; used to query whether an address is reserved by one of them.
static Platform::Mutex memoriesMutex;
static std::vector<Memory*> memories;

enum
{
	numGuardPages = 1
};

static Uptr getPlatformPagesPerWebAssemblyPageLog2()
{
	errorUnless(Platform::getPageSizeLog2() <= IR::numBytesPerPageLog2);
	return IR::numBytesPerPageLog2 - Platform::getPageSizeLog2();
}

static Memory* createMemoryImpl(Compartment* compartment,
								IR::MemoryType type,
								Uptr numPages,
								std::string&& debugName)
{
	Memory* memory = new Memory(compartment, type, std::move(debugName));

	// On a 64-bit runtime, allocate 8GB of address space for the memory.
	// This allows eliding bounds checks on memory accesses, since a 32-bit index + 32-bit offset
	// will always be within the reserved address-space.
	const Uptr pageBytesLog2 = Platform::getPageSizeLog2();
	const Uptr memoryMaxBytes = Uptr(8ull * 1024 * 1024 * 1024);
	const Uptr memoryMaxPages = memoryMaxBytes >> pageBytesLog2;

	memory->baseAddress = Platform::allocateVirtualPages(memoryMaxPages + numGuardPages);
	memory->numReservedBytes = memoryMaxBytes;
	if(!memory->baseAddress)
	{
		delete memory;
		return nullptr;
	}

	// Grow the memory to the type's minimum size.
	if(growMemory(memory, numPages) == -1)
	{
		delete memory;
		return nullptr;
	}

	// Add the memory to the global array.
	{
		Lock<Platform::Mutex> memoriesLock(memoriesMutex);
		memories.push_back(memory);
	}

	return memory;
}

Memory* Runtime::createMemory(Compartment* compartment,
							  IR::MemoryType type,
							  std::string&& debugName)
{
	wavmAssert(type.size.min <= UINTPTR_MAX);
	Memory* memory = createMemoryImpl(compartment, type, Uptr(type.size.min), std::move(debugName));
	if(!memory) { return nullptr; }

	// Add the memory to the compartment's memories IndexMap.
	{
		Lock<Platform::Mutex> compartmentLock(compartment->mutex);

		memory->id = compartment->memories.add(UINTPTR_MAX, memory);
		if(memory->id == UINTPTR_MAX)
		{
			delete memory;
			return nullptr;
		}
		compartment->runtimeData->memoryBases[memory->id] = memory->baseAddress;
	}

	return memory;
}

Memory* Runtime::cloneMemory(Memory* memory, Compartment* newCompartment)
{
	Lock<Platform::Mutex> resizingLock(memory->resizingMutex);
	const Uptr numPages = memory->numPages.load(std::memory_order_acquire);
	std::string debugName = memory->debugName;
	Memory* newMemory
		= createMemoryImpl(newCompartment, memory->type, numPages, std::move(debugName));
	if(!newMemory) { return nullptr; }

	// Copy the memory contents to the new memory.
	memcpy(newMemory->baseAddress, memory->baseAddress, numPages * IR::numBytesPerPage);

	resizingLock.unlock();

	// Insert the memory in the new compartment's memories array with the same index as it had in
	// the original compartment's memories IndexMap.
	{
		Lock<Platform::Mutex> compartmentLock(newCompartment->mutex);

		newMemory->id = memory->id;
		newCompartment->memories.insertOrFail(newMemory->id, newMemory);
		newCompartment->runtimeData->memoryBases[newMemory->id] = newMemory->baseAddress;
	}

	return newMemory;
}

Runtime::Memory::~Memory()
{
	if(id != UINTPTR_MAX)
	{
		wavmAssertMutexIsLockedByCurrentThread(compartment->mutex);

		wavmAssert(compartment->memories[id] == this);
		compartment->memories.removeOrFail(id);

		wavmAssert(compartment->runtimeData->memoryBases[id] == baseAddress);
		compartment->runtimeData->memoryBases[id] = nullptr;
	}

	// Remove the memory from the global array.
	{
		Lock<Platform::Mutex> memoriesLock(memoriesMutex);
		for(Uptr memoryIndex = 0; memoryIndex < memories.size(); ++memoryIndex)
		{
			if(memories[memoryIndex] == this)
			{
				memories.erase(memories.begin() + memoryIndex);
				break;
			}
		}
	}

	// Free the virtual address space.
	const Uptr pageBytesLog2 = Platform::getPageSizeLog2();
	if(numReservedBytes > 0)
	{
		Platform::freeVirtualPages(baseAddress,
								   (numReservedBytes >> pageBytesLog2) + numGuardPages);
	}
	baseAddress = nullptr;
	numPages = numReservedBytes = 0;
}

bool Runtime::isAddressOwnedByMemory(U8* address, Memory*& outMemory, Uptr& outMemoryAddress)
{
	// Iterate over all memories and check if the address is within the reserved address space for
	// each.
	Lock<Platform::Mutex> memoriesLock(memoriesMutex);
	for(auto memory : memories)
	{
		U8* startAddress = memory->baseAddress;
		U8* endAddress = memory->baseAddress + memory->numReservedBytes;
		if(address >= startAddress && address < endAddress)
		{
			outMemory = memory;
			outMemoryAddress = address - startAddress;
			return true;
		}
	}
	return false;
}

Uptr Runtime::getMemoryNumPages(const Memory* memory)
{
	return memory->numPages.load(std::memory_order_seq_cst);
}
IR::MemoryType Runtime::getMemoryType(const Memory* memory) { return memory->type; }

Iptr Runtime::growMemory(Memory* memory, Uptr numPagesToGrow)
{
	if(numPagesToGrow == 0) { return memory->numPages.load(std::memory_order_seq_cst); }

	Lock<Platform::Mutex> resizingLock(memory->resizingMutex);
	const Uptr previousNumPages = memory->numPages.load(std::memory_order_acquire);

	// If the number of pages to grow would cause the memory's size to exceed its maximum,
	// return -1.
	if(numPagesToGrow > memory->type.size.max
	   || previousNumPages > memory->type.size.max - numPagesToGrow
	   || numPagesToGrow > IR::maxMemoryPages
	   || previousNumPages > IR::maxMemoryPages - numPagesToGrow)
	{ return -1; }

	// Try to commit the new pages, and return -1 if the commit fails.
	if(!Platform::commitVirtualPages(memory->baseAddress + previousNumPages * IR::numBytesPerPage,
									 numPagesToGrow << getPlatformPagesPerWebAssemblyPageLog2()))
	{ return -1; }

	memory->numPages.store(previousNumPages + numPagesToGrow, std::memory_order_release);
	return previousNumPages;
}

void Runtime::unmapMemoryPages(Memory* memory, Uptr pageIndex, Uptr numPages)
{
	wavmAssert(pageIndex + numPages > pageIndex);
	wavmAssert((pageIndex + numPages) * IR::numBytesPerPage <= memory->numReservedBytes);

	// Decommit the pages.
	Platform::decommitVirtualPages(memory->baseAddress + pageIndex * IR::numBytesPerPage,
								   numPages << getPlatformPagesPerWebAssemblyPageLog2());
}

U8* Runtime::getMemoryBaseAddress(Memory* memory) { return memory->baseAddress; }

static U8* getValidatedMemoryOffsetRangeImpl(Memory* memory,
											 U8* memoryBase,
											 Uptr memoryNumBytes,
											 Uptr address,
											 Uptr numBytes)
{
	if(address + numBytes > memoryNumBytes || address + numBytes < address)
	{
		throwException(
			ExceptionTypes::outOfBoundsMemoryAccess,
			{asObject(memory), U64(address > memoryNumBytes ? address : memoryNumBytes)});
	}
	wavmAssert(memoryBase);
	numBytes = branchlessMin(numBytes, memoryNumBytes);
	return memoryBase + branchlessMin(address, memoryNumBytes - numBytes);
}

U8* Runtime::getReservedMemoryOffsetRange(Memory* memory, Uptr address, Uptr numBytes)
{
	wavmAssert(memory);

	// Validate that the range [offset..offset+numBytes) is contained by the memory's reserved
	// pages.
	return ::getValidatedMemoryOffsetRangeImpl(
		memory, memory->baseAddress, memory->numReservedBytes, address, numBytes);
}

U8* Runtime::getValidatedMemoryOffsetRange(Memory* memory, Uptr address, Uptr numBytes)
{
	wavmAssert(memory);

	// Validate that the range [offset..offset+numBytes) is contained by the memory's committed
	// pages.
	return ::getValidatedMemoryOffsetRangeImpl(
		memory,
		memory->baseAddress,
		memory->numPages.load(std::memory_order_acquire) * IR::numBytesPerPage,
		address,
		numBytes);
}

void Runtime::initDataSegment(ModuleInstance* moduleInstance,
							  Uptr dataSegmentIndex,
							  const std::vector<U8>* dataVector,
							  Memory* memory,
							  Uptr destAddress,
							  Uptr sourceOffset,
							  Uptr numBytes)
{
	U8* destPointer = getReservedMemoryOffsetRange(memory, destAddress, numBytes);
	if(numBytes)
	{
		if(sourceOffset + numBytes > dataVector->size() || sourceOffset + numBytes < sourceOffset)
		{
			// If the source range is outside the bounds of the data segment, copy the part
			// that is in range, then trap.
			if(sourceOffset < dataVector->size())
			{
				Runtime::unwindSignalsAsExceptions([destPointer, sourceOffset, dataVector] {
					bytewiseMemCopy(destPointer,
									dataVector->data() + sourceOffset,
									dataVector->size() - sourceOffset);
				});
			}
			throwException(
				ExceptionTypes::outOfBoundsDataSegmentAccess,
				{asObject(moduleInstance), U64(dataSegmentIndex), U64(dataVector->size())});
		}
		else
		{
			Runtime::unwindSignalsAsExceptions([destPointer, sourceOffset, numBytes, dataVector] {
				bytewiseMemCopy(destPointer, dataVector->data() + sourceOffset, numBytes);
			});
		}
	}
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wavmIntrinsicsMemory,
							   "memory.grow",
							   I32,
							   memory_grow,
							   U32 deltaPages,
							   Uptr memoryId)
{
	Memory* memory = getMemoryFromRuntimeData(contextRuntimeData, memoryId);
	const Iptr numPreviousMemoryPages = growMemory(memory, (Uptr)deltaPages);
	wavmAssert(numPreviousMemoryPages <= INT32_MAX);
	return I32(numPreviousMemoryPages);
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wavmIntrinsicsMemory, "memory.size", U32, memory_size, I64 memoryId)
{
	Memory* memory = getMemoryFromRuntimeData(contextRuntimeData, memoryId);
	Uptr numMemoryPages = getMemoryNumPages(memory);
	wavmAssert(numMemoryPages <= UINT32_MAX);
	return U32(numMemoryPages);
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wavmIntrinsicsMemory,
							   "memory.init",
							   void,
							   memory_init,
							   U32 destAddress,
							   U32 sourceOffset,
							   U32 numBytes,
							   Uptr moduleInstanceId,
							   Uptr memoryId,
							   Uptr dataSegmentIndex)
{
	ModuleInstance* moduleInstance
		= getModuleInstanceFromRuntimeData(contextRuntimeData, moduleInstanceId);
	Memory* memory = getMemoryFromRuntimeData(contextRuntimeData, memoryId);

	Lock<Platform::Mutex> dataSegmentsLock(moduleInstance->dataSegmentsMutex);
	if(!moduleInstance->dataSegments[dataSegmentIndex])
	{ throwException(ExceptionTypes::invalidArgument); }
	else
	{
		// Make a copy of the shared_ptr to the data and unlock the data segments mutex.
		std::shared_ptr<std::vector<U8>> dataVector
			= moduleInstance->dataSegments[dataSegmentIndex];
		dataSegmentsLock.unlock();

		initDataSegment(moduleInstance,
						dataSegmentIndex,
						dataVector.get(),
						memory,
						destAddress,
						sourceOffset,
						numBytes);
	}
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wavmIntrinsicsMemory,
							   "data.drop",
							   void,
							   data_drop,
							   Uptr moduleInstanceId,
							   Uptr dataSegmentIndex)
{
	ModuleInstance* moduleInstance
		= getModuleInstanceFromRuntimeData(contextRuntimeData, moduleInstanceId);
	Lock<Platform::Mutex> dataSegmentsLock(moduleInstance->dataSegmentsMutex);

	if(!moduleInstance->dataSegments[dataSegmentIndex])
	{ throwException(ExceptionTypes::invalidArgument); }
	else
	{
		moduleInstance->dataSegments[dataSegmentIndex].reset();
	}
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wavmIntrinsicsMemory,
							   "memory.copy",
							   void,
							   memory_copy,
							   U32 destAddress,
							   U32 sourceAddress,
							   U32 numBytes,
							   Uptr sourceMemoryId,
							   Uptr destMemoryId)
{
	Memory* sourceMemory = getMemoryFromRuntimeData(contextRuntimeData, sourceMemoryId);
	Memory* destMemory = getMemoryFromRuntimeData(contextRuntimeData, destMemoryId);

	U8* destPointer = getReservedMemoryOffsetRange(destMemory, destAddress, numBytes);
	U8* sourcePointer = getReservedMemoryOffsetRange(sourceMemory, sourceAddress, numBytes);

	unwindSignalsAsExceptions([=] { bytewiseMemMove(destPointer, sourcePointer, numBytes); });
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wavmIntrinsicsMemory,
							   "memory.fill",
							   void,
							   memory_fill,
							   U32 destAddress,
							   U32 value,
							   U32 numBytes,
							   Uptr memoryId)
{
	Memory* memory = getMemoryFromRuntimeData(contextRuntimeData, memoryId);

	U8* destPointer = getReservedMemoryOffsetRange(memory, destAddress, numBytes);
	unwindSignalsAsExceptions([=] { bytewiseMemSet(destPointer, U8(value), numBytes); });
}
