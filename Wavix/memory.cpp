#include <algorithm>

#include "WAVM/IR/IR.h"
#include "WAVM/Inline/BasicTypes.h"
#include "WAVM/Runtime/Intrinsics.h"
#include "WAVM/Runtime/Runtime.h"
#include "errno.h"
#include "process.h"
#include "wavix.h"

using namespace WAVM::Runtime;
using namespace Wavix;

namespace Wavix {
	void staticInitializeMemory() {}
}

DEFINE_INTRINSIC_FUNCTION(wavix,
						  "__syscall_mmap",
						  I32,
						  __syscall_mmap,
						  I32 address,
						  I32 numBytes,
						  I32 prot,
						  I32 flags,
						  I32 fd,
						  I32 offset)
{
	traceSyscallf("mmap",
				  "(address=0x%08x, numBytes=%u, prot=0x%x, flags=0x%x, fd=%i, offset=%u)",
				  address,
				  numBytes,
				  prot,
				  flags,
				  fd,
				  offset);

	const Uptr numPages = (Uptr(numBytes) + IR::numBytesPerPage - 1) / IR::numBytesPerPage;

	if(address != 0 || fd != -1) { throwException(Exception::calledUnimplementedIntrinsicType); }

	MemoryInstance* memory = currentThread->process->memory;
	Iptr basePageIndex = growMemory(memory, numPages);
	if(basePageIndex == -1) { return -ErrNo::enomem; }

	return coerce32bitAddress(Uptr(basePageIndex) * IR::numBytesPerPage);
}

DEFINE_INTRINSIC_FUNCTION(wavix,
						  "__syscall_munmap",
						  I32,
						  __syscall_munmap,
						  I32 address,
						  I32 numBytes)
{
	traceSyscallf("munmap", "(address=0x%08x, numBytes=%u)", address, numBytes);

	MemoryInstance* memory = currentThread->process->memory;

	if(address & (IR::numBytesPerPage - 1) || numBytes == 0) { return -ErrNo::einval; }

	const Uptr basePageIndex = address / IR::numBytesPerPage;
	const Uptr numPages = (numBytes + IR::numBytesPerPage - 1) / IR::numBytesPerPage;

	if(basePageIndex + numPages > getMemoryMaxPages(memory)) { return -ErrNo::einval; }

	unmapMemoryPages(memory, basePageIndex, numPages);

	return 0;
}

#define WAVIX_MREMAP_MAYMOVE 1
#define WAVIX_MREMAP_FIXED 2

DEFINE_INTRINSIC_FUNCTION(wavix,
						  "__syscall_mremap",
						  U32,
						  __syscall_mremap,
						  U32 oldAddress,
						  U32 oldNumBytes,
						  U32 newNumBytes,
						  I32 flags,
						  U32 newAddress)
{
	traceSyscallf(
		"mremap",
		"(oldAddress=0x%08x, oldNumBytes=%u, newNumBytes=%u, flags=0x%x, newAddress=0x%08x)",
		oldAddress,
		oldNumBytes,
		newNumBytes,
		flags,
		newAddress);

	MemoryInstance* memory = currentThread->process->memory;

	if(flags & WAVIX_MREMAP_FIXED) { return -ErrNo::enomem; }

	if(oldAddress & (IR::numBytesPerPage - 1)) { return -ErrNo::einval; }

	// Round newNumBytes up to the next multiple of the page size.
	const Uptr newNumPages = (newNumBytes + IR::numBytesPerPage - 1) / IR::numBytesPerPage;

	const Iptr newPageIndex = growMemory(memory, newNumPages);
	if(newPageIndex < 0) { return -ErrNo::enomem; }

	newAddress = coerce32bitAddress(Uptr(newPageIndex) * IR::numBytesPerPage);

	const U32 numBytesToCopy = std::min(oldNumBytes, newNumBytes);

	WAVM::Platform::bytewiseMemCopy(memoryArrayPtr<U8>(memory, newAddress, numBytesToCopy),
									memoryArrayPtr<U8>(memory, oldAddress, numBytesToCopy),
									numBytesToCopy);

	unmapMemoryPages(memory,
					 oldAddress / IR::numBytesPerPage,
					 (oldNumBytes + IR::numBytesPerPage - 1) / IR::numBytesPerPage);

	return newAddress;
}

#define WAVIX_MADV_NORMAL 0
#define WAVIX_MADV_RANDOM 1
#define WAVIX_MADV_SEQUENTIAL 2
#define WAVIX_MADV_WILLNEED 3
#define WAVIX_MADV_DONTNEED 4

DEFINE_INTRINSIC_FUNCTION(wavix,
						  "__syscall_madvise",
						  I32,
						  __syscall_madvise,
						  I32 address,
						  I32 numBytes,
						  I32 advice)
{
	traceSyscallf("madvise", "(address=0x%08x, numBytes=%u, advise=%u)", address, numBytes, advice);

	MemoryInstance* memory = currentThread->process->memory;

	if((address & (IR::numBytesPerPage - 1))) { return -ErrNo::einval; }

	// Round numBytes up to the next multiple of the page size.
	numBytes = (numBytes + IR::numBytesPerPage - 1) & -IR::numBytesPerPage;

	if(advice == WAVIX_MADV_DONTNEED)
	{
		WAVM::Platform::bytewiseMemSet(memoryArrayPtr<U8>(memory, address, numBytes), 0, numBytes);
		return 0;
	}
	else
	{
		throwException(Exception::calledUnimplementedIntrinsicType);
	}
}

DEFINE_INTRINSIC_FUNCTION(wavix, "__syscall_brk", I32, __syscall_brk, I32 address)
{
	MemoryInstance* memory = currentThread->process->memory;

	traceSyscallf("brk", "(address=0x%08x)", address);
	// throwException(Exception::calledUnimplementedIntrinsicType);

	return coerce32bitAddress(getMemoryNumPages(memory));
}
