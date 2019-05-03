#include "WAVM/Platform/Memory.h"
#include "WAVM/Inline/Assert.h"
#include "WAVM/Inline/BasicTypes.h"
#include "WAVM/Inline/Errors.h"
#include "WAVM/Platform/Intrinsic.h"

#define NOMINMAX
#include <Windows.h>

using namespace WAVM;
using namespace WAVM::Platform;

static Uptr internalGetPreferredVirtualPageSizeLog2()
{
	SYSTEM_INFO systemInfo;
	GetSystemInfo(&systemInfo);
	Uptr preferredVirtualPageSize = systemInfo.dwPageSize;
	// Verify our assumption that the virtual page size is a power of two.
	errorUnless(!(preferredVirtualPageSize & (preferredVirtualPageSize - 1)));
	return floorLogTwo(preferredVirtualPageSize);
}
Uptr Platform::getPageSizeLog2()
{
	static Uptr preferredVirtualPageSizeLog2 = internalGetPreferredVirtualPageSizeLog2();
	return preferredVirtualPageSizeLog2;
}

static U32 memoryAccessAsWin32Flag(MemoryAccess access)
{
	switch(access)
	{
	default:
	case MemoryAccess::none: return PAGE_NOACCESS;
	case MemoryAccess::readOnly: return PAGE_READONLY;
	case MemoryAccess::readWrite: return PAGE_READWRITE;
	case MemoryAccess::execute: return PAGE_EXECUTE_READ;
	case MemoryAccess::readWriteExecute: return PAGE_EXECUTE_READWRITE;
	}
}

static bool isPageAligned(U8* address)
{
	const Uptr addressBits = reinterpret_cast<Uptr>(address);
	return (addressBits & ((1ull << getPageSizeLog2()) - 1)) == 0;
}

U8* Platform::allocateVirtualPages(Uptr numPages)
{
	const Uptr pageSizeLog2 = getPageSizeLog2();
	const Uptr numBytes = numPages << pageSizeLog2;
	void* result = VirtualAlloc(nullptr, numBytes, MEM_RESERVE, PAGE_NOACCESS);
	if(result == nullptr) { return nullptr; }
	return (U8*)result;
}

U8* Platform::allocateAlignedVirtualPages(Uptr numPages,
										  Uptr alignmentLog2,
										  U8*& outUnalignedBaseAddress)
{
	const Uptr pageSizeLog2 = getPageSizeLog2();
	const Uptr numBytes = numPages << pageSizeLog2;
	if(alignmentLog2 > pageSizeLog2)
	{
		Uptr numTries = 0;
		while(true)
		{
			// Call VirtualAlloc with enough padding added to the size to align the allocation
			// within the unaligned mapping.
			const Uptr alignmentBytes = 1ull << alignmentLog2;
			void* probeResult
				= VirtualAlloc(nullptr, numBytes + alignmentBytes, MEM_RESERVE, PAGE_NOACCESS);
			if(!probeResult) { return nullptr; }

			const Uptr address = reinterpret_cast<Uptr>(probeResult);
			const Uptr alignedAddress = (address + alignmentBytes - 1) & ~(alignmentBytes - 1);

			if(numTries < 10)
			{
				// Free the unaligned+padded allocation, and try to immediately reserve just the
				// aligned middle part again. This can fail due to races with other threads, so
				// handle the VirtualAlloc failing by just retrying with a new unaligned+padded
				// allocation.
				errorUnless(VirtualFree(probeResult, 0, MEM_RELEASE));
				outUnalignedBaseAddress = (U8*)VirtualAlloc(
					reinterpret_cast<void*>(alignedAddress), numBytes, MEM_RESERVE, PAGE_NOACCESS);
				if(outUnalignedBaseAddress) { return outUnalignedBaseAddress; }

				++numTries;
			}
			else
			{
				// If the below free and re-alloc of the aligned address fails too many times,
				// just return the padded allocation.
				outUnalignedBaseAddress = (U8*)probeResult;
				return reinterpret_cast<U8*>(alignedAddress);
			}
		}
	}
	else
	{
		outUnalignedBaseAddress = allocateVirtualPages(numPages);
		return outUnalignedBaseAddress;
	}
}

bool Platform::commitVirtualPages(U8* baseVirtualAddress, Uptr numPages, MemoryAccess access)
{
	errorUnless(isPageAligned(baseVirtualAddress));
	return baseVirtualAddress
		   == VirtualAlloc(baseVirtualAddress,
						   numPages << getPageSizeLog2(),
						   MEM_COMMIT,
						   memoryAccessAsWin32Flag(access));
}

bool Platform::setVirtualPageAccess(U8* baseVirtualAddress, Uptr numPages, MemoryAccess access)
{
	errorUnless(isPageAligned(baseVirtualAddress));
	DWORD oldProtection = 0;
	return VirtualProtect(baseVirtualAddress,
						  numPages << getPageSizeLog2(),
						  memoryAccessAsWin32Flag(access),
						  &oldProtection)
		   != 0;
}

void Platform::decommitVirtualPages(U8* baseVirtualAddress, Uptr numPages)
{
	errorUnless(isPageAligned(baseVirtualAddress));
	auto result = VirtualFree(baseVirtualAddress, numPages << getPageSizeLog2(), MEM_DECOMMIT);
	if(baseVirtualAddress && !result) { Errors::fatal("VirtualFree(MEM_DECOMMIT) failed"); }
}

void Platform::freeVirtualPages(U8* baseVirtualAddress, Uptr numPages)
{
	errorUnless(isPageAligned(baseVirtualAddress));
	auto result = VirtualFree(baseVirtualAddress, 0, MEM_RELEASE);
	if(baseVirtualAddress && !result) { Errors::fatal("VirtualFree(MEM_RELEASE) failed"); }
}

void Platform::freeAlignedVirtualPages(U8* unalignedBaseAddress, Uptr numPages, Uptr alignmentLog2)
{
	errorUnless(isPageAligned(unalignedBaseAddress));
	auto result = VirtualFree(unalignedBaseAddress, 0, MEM_RELEASE);
	if(unalignedBaseAddress && !result) { Errors::fatal("VirtualFree(MEM_RELEASE) failed"); }
}
