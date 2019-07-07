#include "./WASIPrivate.h"
#include "./WASITypes.h"
#include "WAVM/Inline/BasicTypes.h"
#include "WAVM/Logging/Logging.h"
#include "WAVM/Platform/Clock.h"
#include "WAVM/Runtime/Runtime.h"
#include "WAVM/VFS/VFS.h"

using namespace WAVM;
using namespace WAVM::WASI;
using namespace WAVM::Runtime;
using namespace WAVM::VFS;

namespace WAVM { namespace WASI {
	WAVM_DEFINE_INTRINSIC_MODULE(wasiFile)
}}

static __wasi_errno_t asWASIErrNo(VFS::Result result)
{
	switch(result)
	{
	case Result::success: return __WASI_ESUCCESS;
	case Result::ioPending: return __WASI_EINPROGRESS;
	case Result::ioDeviceError: return __WASI_EIO;
	case Result::interruptedBySignal: return __WASI_EINTR;
	case Result::interruptedByCancellation: return __WASI_EINTR;
	case Result::wouldBlock: return __WASI_EAGAIN;
	case Result::inaccessibleBuffer: return __WASI_EFAULT;
	case Result::invalidOffset: return __WASI_EINVAL;
	case Result::notSeekable: return __WASI_ESPIPE;
	case Result::notPermitted: return __WASI_EPERM;
	case Result::notAccessible: return __WASI_EACCES;
	case Result::notSynchronizable: return __WASI_EINVAL;
	case Result::tooManyBufferBytes: return __WASI_EINVAL;
	case Result::notEnoughBufferBytes: return __WASI_EINVAL;
	case Result::tooManyBuffers: return __WASI_EINVAL;
	case Result::notEnoughBits: return __WASI_EOVERFLOW;
	case Result::exceededFileSizeLimit: return __WASI_EFBIG;
	case Result::outOfSystemFDs: return __WASI_ENFILE;
	case Result::outOfProcessFDs: return __WASI_EMFILE;
	case Result::outOfMemory: return __WASI_ENOMEM;
	case Result::outOfQuota: return __WASI_EDQUOT;
	case Result::outOfFreeSpace: return __WASI_ENOSPC;
	case Result::outOfLinksToParentDir: return __WASI_EMLINK;
	case Result::invalidNameCharacter: return __WASI_EACCES;
	case Result::nameTooLong: return __WASI_ENAMETOOLONG;
	case Result::tooManyLinksInPath: return __WASI_ELOOP;
	case Result::alreadyExists: return __WASI_EEXIST;
	case Result::doesNotExist: return __WASI_ENOENT;
	case Result::isDirectory: return __WASI_EISDIR;
	case Result::isNotDirectory: return __WASI_ENOTDIR;
	case Result::isNotEmpty: return __WASI_ENOTEMPTY;
	case Result::brokenPipe: return __WASI_EPIPE;
	case Result::missingDevice: return __WASI_ENXIO;
	case Result::busy: return __WASI_EBUSY;

	default: WAVM_UNREACHABLE();
	};
}

static __wasi_errno_t validateFD(Process* process,
								 __wasi_fd_t fd,
								 __wasi_rights_t requiredRights,
								 __wasi_rights_t requiredInheritingRights,
								 WASI::FDE*& outFDE)
{
	if(fd < process->fds.getMinIndex() || fd > process->fds.getMaxIndex()) { return __WASI_EBADF; }
	WASI::FDE* fde = process->fds.get(fd);
	if(!fde) { return __WASI_EBADF; }

	if((fde->rights & requiredRights) != requiredRights
	   || (fde->inheritingRights & requiredInheritingRights) != requiredInheritingRights)
	{ return __WASI_ENOTCAPABLE; }

	outFDE = fde;
	return __WASI_ESUCCESS;
}

static __wasi_filetype_t asWASIFileType(FileType type)
{
	switch(type)
	{
	case FileType::unknown: return __WASI_FILETYPE_UNKNOWN;
	case FileType::blockDevice: return __WASI_FILETYPE_BLOCK_DEVICE;
	case FileType::characterDevice: return __WASI_FILETYPE_CHARACTER_DEVICE;
	case FileType::directory: return __WASI_FILETYPE_DIRECTORY;
	case FileType::file: return __WASI_FILETYPE_REGULAR_FILE;
	case FileType::datagramSocket: return __WASI_FILETYPE_SOCKET_DGRAM;
	case FileType::streamSocket: return __WASI_FILETYPE_SOCKET_STREAM;
	case FileType::symbolicLink: return __WASI_FILETYPE_SYMBOLIC_LINK;
	case FileType::pipe: return __WASI_FILETYPE_UNKNOWN;

	default: WAVM_UNREACHABLE();
	};
}

static bool readUserString(Memory* memory,
						   WASIAddress stringAddress,
						   WASIAddress numStringBytes,
						   std::string& outString)
{
	outString = "";

	bool succeeded = true;
	catchRuntimeExceptions(
		[&] {
			char* stringBytes = memoryArrayPtr<char>(memory, stringAddress, numStringBytes);
			for(Uptr index = 0; index < numStringBytes; ++index)
			{ outString += stringBytes[index]; }
		},
		[&succeeded](Exception* exception) {
			errorUnless(getExceptionType(exception) == ExceptionTypes::outOfBoundsMemoryAccess);
			Log::printf(Log::debug,
						"Caught runtime exception while reading string at address 0x%" PRIx64,
						getExceptionArgument(exception, 1).i64);
			destroyException(exception);

			succeeded = false;
		});

	return succeeded;
}

static bool getCanonicalPath(const std::string& basePath,
							 const std::string& relativePath,
							 std::string& outAbsolutePath)
{
	outAbsolutePath = basePath;
	if(outAbsolutePath.back() == '/') { outAbsolutePath.pop_back(); }

	std::vector<std::string> relativePathComponents;

	Uptr componentStart = 0;
	while(componentStart < relativePath.size())
	{
		while(componentStart < relativePath.size() && relativePath[componentStart] == '/')
		{ ++componentStart; }

		Uptr nextPathSeparator = relativePath.find_first_of('/', componentStart);

		if(nextPathSeparator == std::string::npos) { nextPathSeparator = relativePath.size(); }

		if(nextPathSeparator != componentStart)
		{
			std::string component
				= relativePath.substr(componentStart, nextPathSeparator - componentStart);

			if(component == "..")
			{
				if(!relativePathComponents.size()) { return false; }
				else
				{
					relativePathComponents.pop_back();
				}
			}
			else if(component != ".")
			{
				relativePathComponents.push_back(component);
			}

			componentStart = nextPathSeparator + 1;
		}
	};

	for(const std::string& component : relativePathComponents)
	{
		outAbsolutePath += '/';
		outAbsolutePath += component;
	}

	return true;
}

static __wasi_errno_t validatePath(Process* process,
								   __wasi_fd_t dirFD,
								   __wasi_lookupflags_t lookupFlags,
								   __wasi_rights_t requiredDirRights,
								   __wasi_rights_t requiredDirInheritingRights,
								   WASIAddress pathAddress,
								   WASIAddress numPathBytes,
								   std::string& outCanonicalPath)
{
	if(!process->fileSystem) { return __WASI_ENOTCAPABLE; }

	WASI::FDE* dirFDE = nullptr;
	const __wasi_errno_t fdError
		= validateFD(process, dirFD, requiredDirRights, requiredDirInheritingRights, dirFDE);
	if(fdError != __WASI_ESUCCESS) { return fdError; }

	std::string relativePath;
	if(!readUserString(process->memory, pathAddress, numPathBytes, relativePath))
	{ return __WASI_EFAULT; }

	if(!getCanonicalPath(dirFDE->originalPath, relativePath, outCanonicalPath))
	{ return __WASI_ENOTCAPABLE; }

	return __WASI_ESUCCESS;
}

static VFDFlags translateWASIVFDFlags(__wasi_fdflags_t fdFlags, __wasi_rights_t& outRequiredRights)
{
	VFDFlags result;
	if(fdFlags & __WASI_FDFLAG_DSYNC)
	{
		result.syncLevel = (fdFlags & __WASI_FDFLAG_RSYNC)
							   ? VFDSync::contentsAfterWriteAndBeforeRead
							   : VFDSync::contentsAfterWrite;
		outRequiredRights |= __WASI_RIGHT_FD_DATASYNC;
	}
	if(fdFlags & __WASI_FDFLAG_SYNC)
	{
		result.syncLevel = (fdFlags & __WASI_FDFLAG_RSYNC)
							   ? VFDSync::contentsAndMetadataAfterWriteAndBeforeRead
							   : VFDSync::contentsAndMetadataAfterWrite;
		outRequiredRights |= __WASI_RIGHT_FD_SYNC;
	}
	if(fdFlags & __WASI_FDFLAG_NONBLOCK) { result.nonBlocking = true; }
	if(fdFlags & __WASI_FDFLAG_APPEND) { result.append = true; }
	return result;
}

Result WASI::FDE::close() const
{
	Result result = vfd->close();
	if(result == VFS::Result::success && dirEntStream) { dirEntStream->close(); }
	return result;
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wasiFile,
							   "fd_prestat_get",
							   __wasi_errno_return_t,
							   wasi_fd_prestat_get,
							   __wasi_fd_t fd,
							   WASIAddress prestatAddress)
{
	TRACE_SYSCALL("fd_prestat_get", "(%u, " WASIADDRESS_FORMAT ")", fd, prestatAddress);

	Process* process = getProcessFromContextRuntimeData(contextRuntimeData);

	WASI::FDE* fde = nullptr;
	const __wasi_errno_t fdError = validateFD(process, fd, 0, 0, fde);
	if(fdError != __WASI_ESUCCESS) { return TRACE_SYSCALL_RETURN(fdError); }

	if(fde->originalPath.size() > UINT32_MAX) { return TRACE_SYSCALL_RETURN(__WASI_EOVERFLOW); }

	__wasi_prestat_t& prestat = memoryRef<__wasi_prestat_t>(process->memory, prestatAddress);
	prestat.pr_type = fde->preopenedType;
	wavmAssert(fde->preopenedType == __WASI_PREOPENTYPE_DIR);
	prestat.u.dir.pr_name_len = U32(fde->originalPath.size());

	return TRACE_SYSCALL_RETURN(__WASI_ESUCCESS);
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wasiFile,
							   "fd_prestat_dir_name",
							   __wasi_errno_return_t,
							   wasi_fd_prestat_dir_name,
							   __wasi_fd_t fd,
							   WASIAddress bufferAddress,
							   WASIAddress bufferLength)
{
	TRACE_SYSCALL(
		"fd_prestat_dir_name", "(%u, " WASIADDRESS_FORMAT ", %u)", fd, bufferAddress, bufferLength);

	Process* process = getProcessFromContextRuntimeData(contextRuntimeData);

	WASI::FDE* fde = nullptr;
	const __wasi_errno_t fdError = validateFD(process, fd, 0, 0, fde);
	if(fdError != __WASI_ESUCCESS) { return TRACE_SYSCALL_RETURN(fdError); }

	if(!fde->isPreopened) { return TRACE_SYSCALL_RETURN(__WASI_EBADF); }

	if(bufferLength != fde->originalPath.size()) { return TRACE_SYSCALL_RETURN(__WASI_EINVAL); }

	char* buffer = memoryArrayPtr<char>(process->memory, bufferAddress, bufferLength);
	memcpy(buffer, fde->originalPath.c_str(), bufferLength);

	return TRACE_SYSCALL_RETURN(__WASI_ESUCCESS);
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wasiFile,
							   "fd_close",
							   __wasi_errno_return_t,
							   wasi_fd_close,
							   __wasi_fd_t fd)
{
	TRACE_SYSCALL("fd_close", "(%u)", fd);

	Process* process = getProcessFromContextRuntimeData(contextRuntimeData);

	WASI::FDE* fde = nullptr;
	const __wasi_errno_t fdError = validateFD(process, fd, 0, 0, fde);
	if(fdError != __WASI_ESUCCESS) { return TRACE_SYSCALL_RETURN(fdError); }

	if(fde->isPreopened) { return TRACE_SYSCALL_RETURN(__WASI_EBADF); }

	const VFS::Result result = fde->close();

	if(result == VFS::Result::success)
	{
		// If the close succeeded, remove the fd from the fds map.
		process->fds.removeOrFail(fd);
	}

	return TRACE_SYSCALL_RETURN(asWASIErrNo(result));
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wasiFile,
							   "fd_datasync",
							   __wasi_errno_return_t,
							   wasi_fd_datasync,
							   __wasi_fd_t fd)
{
	TRACE_SYSCALL("fd_datasync", "(%u)", fd);

	Process* process = getProcessFromContextRuntimeData(contextRuntimeData);

	WASI::FDE* fde = nullptr;
	const __wasi_errno_t fdError = validateFD(process, fd, __WASI_RIGHT_FD_DATASYNC, 0, fde);
	if(fdError != __WASI_ESUCCESS) { return TRACE_SYSCALL_RETURN(fdError); }

	return TRACE_SYSCALL_RETURN(asWASIErrNo(fde->vfd->sync(SyncType::contents)));
}

static __wasi_errno_t readImpl(Process* process,
							   __wasi_fd_t fd,
							   WASIAddress iovsAddress,
							   I32 numIOVs,
							   const __wasi_filesize_t* offset,
							   Uptr& outNumBytesRead)
{
	WASI::FDE* fde = nullptr;
	const __wasi_rights_t requiredRights
		= __WASI_RIGHT_FD_READ | (offset ? __WASI_RIGHT_FD_SEEK : 0);
	const __wasi_errno_t fdError = validateFD(process, fd, requiredRights, 0, fde);
	if(fdError != __WASI_ESUCCESS) { return fdError; }

	if(numIOVs < 0 || numIOVs > __WASI_IOV_MAX) { return __WASI_EINVAL; }

	// Allocate memory for the IOReadBuffers.
	IOReadBuffer* vfsReadBuffers = (IOReadBuffer*)malloc(numIOVs * sizeof(IOReadBuffer));

	// Catch any out-of-bounds memory access exceptions that are thrown.
	__wasi_errno_t result = __WASI_ESUCCESS;
	Runtime::catchRuntimeExceptions(
		[&] {
			// Translate the IOVs to IOReadBuffers.
			const __wasi_iovec_t* iovs
				= memoryArrayPtr<__wasi_iovec_t>(process->memory, iovsAddress, numIOVs);
			U64 numBufferBytes = 0;
			for(I32 iovIndex = 0; iovIndex < numIOVs; ++iovIndex)
			{
				__wasi_iovec_t iov = iovs[iovIndex];
				vfsReadBuffers[iovIndex].data
					= memoryArrayPtr<U8>(process->memory, iov.buf, iov.buf_len);
				vfsReadBuffers[iovIndex].numBytes = iov.buf_len;
				numBufferBytes += iov.buf_len;
			}
			if(numBufferBytes > WASIADDRESS_MAX) { result = __WASI_EOVERFLOW; }
			else
			{
				// Do the read.
				result = asWASIErrNo(
					fde->vfd->readv(vfsReadBuffers, numIOVs, &outNumBytesRead, offset));
			}
		},
		[&](Exception* exception) {
			// If we catch an out-of-bounds memory exception, return EFAULT.
			errorUnless(getExceptionType(exception) == ExceptionTypes::outOfBoundsMemoryAccess);
			Log::printf(Log::debug,
						"Caught runtime exception while reading memory at address 0x%" PRIx64,
						getExceptionArgument(exception, 1).i64);
			destroyException(exception);
			result = __WASI_EFAULT;
		});

	// Free the VFS read buffers.
	free(vfsReadBuffers);

	return result;
}

static __wasi_errno_t writeImpl(Process* process,
								__wasi_fd_t fd,
								WASIAddress iovsAddress,
								I32 numIOVs,
								const __wasi_filesize_t* offset,
								Uptr& outNumBytesWritten)
{
	WASI::FDE* fde = nullptr;
	const __wasi_rights_t requiredRights
		= __WASI_RIGHT_FD_WRITE | (offset ? __WASI_RIGHT_FD_SEEK : 0);
	const __wasi_errno_t fdError = validateFD(process, fd, requiredRights, 0, fde);
	if(fdError != __WASI_ESUCCESS) { return fdError; }

	if(numIOVs < 0 || numIOVs > __WASI_IOV_MAX) { return __WASI_EINVAL; }

	// Allocate memory for the IOWriteBuffers.
	IOWriteBuffer* vfsWriteBuffers = (IOWriteBuffer*)malloc(numIOVs * sizeof(IOWriteBuffer));

	// Catch any out-of-bounds memory access exceptions that are thrown.
	__wasi_errno_t result = __WASI_ESUCCESS;
	Runtime::catchRuntimeExceptions(
		[&] {
			// Translate the IOVs to IOWriteBuffers
			const __wasi_ciovec_t* iovs
				= memoryArrayPtr<__wasi_ciovec_t>(process->memory, iovsAddress, numIOVs);
			U64 numBufferBytes = 0;
			for(I32 iovIndex = 0; iovIndex < numIOVs; ++iovIndex)
			{
				__wasi_ciovec_t iov = iovs[iovIndex];
				vfsWriteBuffers[iovIndex].data
					= memoryArrayPtr<const U8>(process->memory, iov.buf, iov.buf_len);
				vfsWriteBuffers[iovIndex].numBytes = iov.buf_len;
				numBufferBytes += iov.buf_len;
			}
			if(numBufferBytes > WASIADDRESS_MAX) { result = __WASI_EOVERFLOW; }
			else
			{
				// Do the writes.
				result = asWASIErrNo(
					fde->vfd->writev(vfsWriteBuffers, numIOVs, &outNumBytesWritten, offset));
			}
		},
		[&](Exception* exception) {
			// If we catch an out-of-bounds memory exception, return EFAULT.
			errorUnless(getExceptionType(exception) == ExceptionTypes::outOfBoundsMemoryAccess);
			Log::printf(Log::debug,
						"Caught runtime exception while reading memory at address 0x%" PRIx64,
						getExceptionArgument(exception, 1).i64);
			destroyException(exception);
			result = __WASI_EFAULT;
		});

	// Free the VFS write buffers.
	free(vfsWriteBuffers);

	return result;
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wasiFile,
							   "fd_pread",
							   __wasi_errno_return_t,
							   wasi_fd_pread,
							   __wasi_fd_t fd,
							   WASIAddress iovsAddress,
							   WASIAddress numIOVs,
							   __wasi_filesize_t offset,
							   WASIAddress numBytesReadAddress)
{
	TRACE_SYSCALL("fd_pread",
				  "(%u, " WASIADDRESS_FORMAT ", %u, %" PRIu64 ", " WASIADDRESS_FORMAT ")",
				  fd,
				  iovsAddress,
				  numIOVs,
				  offset,
				  numBytesReadAddress);

	Process* process = getProcessFromContextRuntimeData(contextRuntimeData);

	Uptr numBytesRead = 0;
	const __wasi_errno_t result
		= readImpl(process, fd, iovsAddress, numIOVs, &offset, numBytesRead);

	// Write the number of bytes read to memory.
	wavmAssert(numBytesRead <= WASIADDRESS_MAX);
	memoryRef<WASIAddress>(process->memory, numBytesReadAddress) = WASIAddress(numBytesRead);

	return TRACE_SYSCALL_RETURN(result, " (numBytesRead=%" PRIuPTR ")", numBytesRead);
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wasiFile,
							   "fd_pwrite",
							   __wasi_errno_return_t,
							   wasi_fd_pwrite,
							   __wasi_fd_t fd,
							   WASIAddress iovsAddress,
							   WASIAddress numIOVs,
							   __wasi_filesize_t offset,
							   WASIAddress numBytesWrittenAddress)
{
	TRACE_SYSCALL("fd_pwrite",
				  "(%u, " WASIADDRESS_FORMAT ", %u, %" PRIu64 ", " WASIADDRESS_FORMAT ")",
				  fd,
				  iovsAddress,
				  numIOVs,
				  offset,
				  numBytesWrittenAddress);

	Process* process = getProcessFromContextRuntimeData(contextRuntimeData);

	Uptr numBytesWritten = 0;
	const __wasi_errno_t result
		= writeImpl(process, fd, iovsAddress, numIOVs, &offset, numBytesWritten);

	// Write the number of bytes written to memory.
	wavmAssert(numBytesWritten <= WASIADDRESS_MAX);
	memoryRef<WASIAddress>(process->memory, numBytesWrittenAddress) = WASIAddress(numBytesWritten);

	return TRACE_SYSCALL_RETURN(result, " (numBytesWritten=%" PRIuPTR ")", numBytesWritten);
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wasiFile,
							   "fd_read",
							   __wasi_errno_return_t,
							   wasi_fd_read,
							   __wasi_fd_t fd,
							   WASIAddress iovsAddress,
							   I32 numIOVs,
							   WASIAddress numBytesReadAddress)
{
	TRACE_SYSCALL("fd_read",
				  "(%u, " WASIADDRESS_FORMAT ", %u, " WASIADDRESS_FORMAT ")",
				  fd,
				  iovsAddress,
				  numIOVs,
				  numBytesReadAddress);

	Process* process = getProcessFromContextRuntimeData(contextRuntimeData);

	Uptr numBytesRead = 0;
	const __wasi_errno_t result
		= readImpl(process, fd, iovsAddress, numIOVs, nullptr, numBytesRead);

	// Write the number of bytes read to memory.
	wavmAssert(numBytesRead <= WASIADDRESS_MAX);
	memoryRef<WASIAddress>(process->memory, numBytesReadAddress) = WASIAddress(numBytesRead);

	return TRACE_SYSCALL_RETURN(result, " (numBytesRead=%" PRIuPTR ")", numBytesRead);
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wasiFile,
							   "fd_write",
							   __wasi_errno_return_t,
							   wasi_fd_write,
							   __wasi_fd_t fd,
							   WASIAddress iovsAddress,
							   I32 numIOVs,
							   WASIAddress numBytesWrittenAddress)
{
	TRACE_SYSCALL("fd_write",
				  "(%u, " WASIADDRESS_FORMAT ", %u, " WASIADDRESS_FORMAT ")",
				  fd,
				  iovsAddress,
				  numIOVs,
				  numBytesWrittenAddress);

	Process* process = getProcessFromContextRuntimeData(contextRuntimeData);

	Uptr numBytesWritten = 0;
	const __wasi_errno_t result
		= writeImpl(process, fd, iovsAddress, numIOVs, nullptr, numBytesWritten);

	// Write the number of bytes written to memory.
	wavmAssert(numBytesWritten <= WASIADDRESS_MAX);
	memoryRef<WASIAddress>(process->memory, numBytesWrittenAddress) = WASIAddress(numBytesWritten);

	return TRACE_SYSCALL_RETURN(result, " (numBytesWritten=%" PRIuPTR ")", numBytesWritten);
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wasiFile,
							   "fd_renumber",
							   __wasi_errno_return_t,
							   wasi_fd_renumber,
							   __wasi_fd_t fromFD,
							   __wasi_fd_t toFD)
{
	TRACE_SYSCALL("fd_renumber", "(%u, %u)", fromFD, toFD);

	Process* process = getProcessFromContextRuntimeData(contextRuntimeData);

	WASI::FDE* fromFDE = nullptr;
	WASI::FDE* toFDE = nullptr;
	__wasi_errno_t fdError = validateFD(process, fromFD, 0, 0, fromFDE);
	if(fdError != __WASI_ESUCCESS) { return TRACE_SYSCALL_RETURN(fdError); }
	fdError = validateFD(process, toFD, 0, 0, toFDE);
	if(fdError != __WASI_ESUCCESS) { return TRACE_SYSCALL_RETURN(fdError); }

	if(fromFDE->isPreopened || toFDE->isPreopened) { return TRACE_SYSCALL_RETURN(__WASI_ENOTSUP); }

	Result result = toFDE->close();
	if(result != VFS::Result::success) { return TRACE_SYSCALL_RETURN(asWASIErrNo(result)); }

	process->fds.insertOrFail(toFD, std::move(*fromFDE));
	process->fds.removeOrFail(toFD);
	process->fds.removeOrFail(fromFD);

	return TRACE_SYSCALL_RETURN(__WASI_ESUCCESS);
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wasiFile,
							   "fd_seek",
							   __wasi_errno_return_t,
							   wasi_fd_seek,
							   __wasi_fd_t fd,
							   __wasi_filedelta_t offset,
							   U32 whence,
							   WASIAddress newOffsetAddress)
{
	TRACE_SYSCALL("fd_seek",
				  "(%u, %" PRIi64 ", %u, " WASIADDRESS_FORMAT ")",
				  fd,
				  offset,
				  whence,
				  newOffsetAddress);

	Process* process = getProcessFromContextRuntimeData(contextRuntimeData);

	WASI::FDE* fde = nullptr;
	const __wasi_errno_t fdError = validateFD(process, fd, __WASI_RIGHT_FD_SEEK, 0, fde);
	if(fdError != __WASI_ESUCCESS) { return TRACE_SYSCALL_RETURN(fdError); }

	SeekOrigin origin;
	switch(whence)
	{
	case __WASI_WHENCE_CUR: origin = SeekOrigin::cur; break;
	case __WASI_WHENCE_END: origin = SeekOrigin::end; break;
	case __WASI_WHENCE_SET: origin = SeekOrigin::begin; break;
	default: return TRACE_SYSCALL_RETURN(__WASI_EINVAL);
	};

	U64 newOffset;
	const VFS::Result result = fde->vfd->seek(offset, origin, &newOffset);
	if(result != VFS::Result::success) { return TRACE_SYSCALL_RETURN(asWASIErrNo(result)); }

	memoryRef<__wasi_filesize_t>(process->memory, newOffsetAddress) = newOffset;
	return TRACE_SYSCALL_RETURN(__WASI_ESUCCESS);
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wasiFile,
							   "fd_tell",
							   __wasi_errno_return_t,
							   wasi_fd_tell,
							   __wasi_fd_t fd,
							   WASIAddress offsetAddress)
{
	TRACE_SYSCALL("fd_tell", "(%u, " WASIADDRESS_FORMAT ")", fd, offsetAddress);

	Process* process = getProcessFromContextRuntimeData(contextRuntimeData);

	WASI::FDE* fde = nullptr;
	const __wasi_errno_t fdError = validateFD(process, fd, __WASI_RIGHT_FD_TELL, 0, fde);
	if(fdError != __WASI_ESUCCESS) { return TRACE_SYSCALL_RETURN(fdError); }

	U64 currentOffset;
	const VFS::Result result = fde->vfd->seek(0, SeekOrigin::cur, &currentOffset);
	if(result != VFS::Result::success) { return TRACE_SYSCALL_RETURN(asWASIErrNo(result)); }

	memoryRef<__wasi_filesize_t>(process->memory, offsetAddress) = currentOffset;
	return TRACE_SYSCALL_RETURN(__WASI_ESUCCESS);
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wasiFile,
							   "fd_fdstat_get",
							   __wasi_errno_return_t,
							   wasi_fd_fdstat_get,
							   __wasi_fd_t fd,
							   WASIAddress fdstatAddress)
{
	TRACE_SYSCALL("fd_fdstat_get", "(%u, " WASIADDRESS_FORMAT ")", fd, fdstatAddress);

	Process* process = getProcessFromContextRuntimeData(contextRuntimeData);

	WASI::FDE* fde = nullptr;
	const __wasi_errno_t fdError = validateFD(process, fd, 0, 0, fde);
	if(fdError != __WASI_ESUCCESS) { return TRACE_SYSCALL_RETURN(fdError); }

	VFDInfo fdInfo;
	const VFS::Result result = fde->vfd->getVFDInfo(fdInfo);
	if(result != VFS::Result::success) { return TRACE_SYSCALL_RETURN(asWASIErrNo(result)); }

	__wasi_fdstat_t& fdstat = memoryRef<__wasi_fdstat_t>(process->memory, fdstatAddress);
	fdstat.fs_filetype = asWASIFileType(fdInfo.type);
	fdstat.fs_flags = 0;

	if(fdInfo.flags.append) { fdstat.fs_flags |= __WASI_FDFLAG_APPEND; }
	if(fdInfo.flags.nonBlocking) { fdstat.fs_flags |= __WASI_FDFLAG_NONBLOCK; }
	switch(fdInfo.flags.syncLevel)
	{
	case VFDSync::none: break;
	case VFDSync::contentsAfterWrite: fdstat.fs_flags |= __WASI_FDFLAG_DSYNC; break;
	case VFDSync::contentsAndMetadataAfterWrite: fdstat.fs_flags |= __WASI_FDFLAG_SYNC; break;
	case VFDSync::contentsAfterWriteAndBeforeRead:
		fdstat.fs_flags |= __WASI_FDFLAG_DSYNC | __WASI_FDFLAG_RSYNC;
		break;
	case VFDSync::contentsAndMetadataAfterWriteAndBeforeRead:
		fdstat.fs_flags |= __WASI_FDFLAG_SYNC | __WASI_FDFLAG_RSYNC;
		break;

	default: WAVM_UNREACHABLE();
	}

	fdstat.fs_rights_base = fde->rights;
	fdstat.fs_rights_inheriting = fde->inheritingRights;

	return TRACE_SYSCALL_RETURN(__WASI_ESUCCESS);
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wasiFile,
							   "fd_fdstat_set_flags",
							   __wasi_errno_return_t,
							   wasi_fd_fdstat_set_flags,
							   __wasi_fd_t fd,
							   __wasi_fdflags_t flags)
{
	TRACE_SYSCALL("fd_fdstat_set_flags", "(%u, 0x%04x)", fd, flags);

	Process* process = getProcessFromContextRuntimeData(contextRuntimeData);

	__wasi_rights_t requiredRights;
	VFDFlags vfsVFDFlags = translateWASIVFDFlags(flags, requiredRights);

	WASI::FDE* fde = nullptr;
	const __wasi_errno_t fdError
		= validateFD(process, fd, __WASI_RIGHT_FD_FDSTAT_SET_FLAGS | requiredRights, 0, fde);
	if(fdError != __WASI_ESUCCESS) { return TRACE_SYSCALL_RETURN(fdError); }

	const VFS::Result result = fde->vfd->setVFDFlags(vfsVFDFlags);
	return TRACE_SYSCALL_RETURN(asWASIErrNo(result));
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wasiFile,
							   "fd_fdstat_set_rights",
							   __wasi_errno_return_t,
							   wasi_fd_fdstat_set_rights,
							   __wasi_fd_t fd,
							   __wasi_rights_t rights,
							   __wasi_rights_t inheritingRights)
{
	TRACE_SYSCALL("fd_fdstat_set_rights",
				  "(%u, 0x%" PRIx64 ", 0x %" PRIx64 ") ",
				  fd,
				  rights,
				  inheritingRights);

	Process* process = getProcessFromContextRuntimeData(contextRuntimeData);

	WASI::FDE* fde = nullptr;
	const __wasi_errno_t fdError = validateFD(process, fd, rights, inheritingRights, fde);
	if(fdError != __WASI_ESUCCESS) { return TRACE_SYSCALL_RETURN(fdError); }

	// Narrow the FD's rights.
	fde->rights = rights;
	fde->inheritingRights = inheritingRights;

	return TRACE_SYSCALL_RETURN(__WASI_ESUCCESS);
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wasiFile,
							   "fd_sync",
							   __wasi_errno_return_t,
							   wasi_fd_sync,
							   __wasi_fd_t fd)
{
	TRACE_SYSCALL("fd_sync", "(%u)", fd);

	Process* process = getProcessFromContextRuntimeData(contextRuntimeData);

	WASI::FDE* fde = nullptr;
	const __wasi_errno_t fdError = validateFD(process, fd, __WASI_RIGHT_FD_SYNC, 0, fde);
	if(fdError != __WASI_ESUCCESS) { return TRACE_SYSCALL_RETURN(fdError); }

	const VFS::Result result = fde->vfd->sync(SyncType::contentsAndMetadata);
	return TRACE_SYSCALL_RETURN(asWASIErrNo(result));
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wasiFile,
							   "fd_advise",
							   __wasi_errno_return_t,
							   wasi_fd_advise,
							   __wasi_fd_t fd,
							   __wasi_filesize_t offset,
							   __wasi_filesize_t numBytes,
							   __wasi_advice_t advice)
{
	TRACE_SYSCALL(
		"fd_advise", "(%u, %" PRIu64 ", %" PRIu64 ", 0x%02x)", fd, offset, numBytes, advice);

	Process* process = getProcessFromContextRuntimeData(contextRuntimeData);

	WASI::FDE* fde = nullptr;
	const __wasi_errno_t fdError = validateFD(process, fd, __WASI_RIGHT_FD_ADVISE, 0, fde);
	if(fdError != __WASI_ESUCCESS) { return TRACE_SYSCALL_RETURN(fdError); }

	switch(advice)
	{
	case __WASI_ADVICE_DONTNEED:
	case __WASI_ADVICE_NOREUSE:
	case __WASI_ADVICE_NORMAL:
	case __WASI_ADVICE_RANDOM:
	case __WASI_ADVICE_SEQUENTIAL:
	case __WASI_ADVICE_WILLNEED:
		// It's safe to ignore the advice, so just return success for now.
		// TODO: do something with the advice!
		return TRACE_SYSCALL_RETURN(__WASI_ESUCCESS);
	default: return TRACE_SYSCALL_RETURN(__WASI_EINVAL);
	}
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wasiFile,
							   "fd_allocate",
							   __wasi_errno_return_t,
							   wasi_fd_allocate,
							   __wasi_fd_t fd,
							   __wasi_filesize_t offset,
							   __wasi_filesize_t numBytes)
{
	UNIMPLEMENTED_SYSCALL("fd_allocate", "(%u, %" PRIu64 ", %" PRIu64 ")", fd, offset, numBytes);
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wasiFile,
							   "path_link",
							   __wasi_errno_return_t,
							   wasi_path_link,
							   __wasi_fd_t dirFD,
							   __wasi_lookupflags_t lookupFlags,
							   WASIAddress oldPathAddress,
							   WASIAddress numOldPathBytes,
							   __wasi_fd_t newFD,
							   WASIAddress newPathAddress,
							   WASIAddress numNewPathBytes)
{
	UNIMPLEMENTED_SYSCALL("path_link",
						  "(%u, 0x%08x, " WASIADDRESS_FORMAT ", %u, %u, " WASIADDRESS_FORMAT
						  ", %u)",
						  dirFD,
						  lookupFlags,
						  oldPathAddress,
						  numOldPathBytes,
						  newFD,
						  newPathAddress,
						  numNewPathBytes);
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wasiFile,
							   "path_open",
							   __wasi_errno_return_t,
							   wasi_path_open,
							   __wasi_fd_t dirFD,
							   __wasi_lookupflags_t lookupFlags,
							   WASIAddress pathAddress,
							   WASIAddress numPathBytes,
							   __wasi_oflags_t openFlags,
							   __wasi_rights_t requestedRights,
							   __wasi_rights_t requestedInheritingRights,
							   __wasi_fdflags_t fdFlags,
							   WASIAddress fdAddress)
{
	TRACE_SYSCALL("path_open",
				  "(%u, 0x%08x, " WASIADDRESS_FORMAT ", %u, 0x%04x, 0x%" PRIx64 ", 0x%" PRIx64
				  ", 0x%04x, " WASIADDRESS_FORMAT ")",
				  dirFD,
				  lookupFlags,
				  pathAddress,
				  numPathBytes,
				  openFlags,
				  requestedRights,
				  requestedInheritingRights,
				  fdFlags,
				  fdAddress);

	Process* process = getProcessFromContextRuntimeData(contextRuntimeData);

	__wasi_rights_t requiredDirRights = __WASI_RIGHT_PATH_OPEN;
	__wasi_rights_t requiredDirInheritingRights = requestedRights | requestedInheritingRights;

	const bool read = requestedRights & (__WASI_RIGHT_FD_READ | __WASI_RIGHT_FD_READDIR);
	const bool write = requestedRights
					   & (__WASI_RIGHT_FD_DATASYNC | __WASI_RIGHT_FD_WRITE
						  | __WASI_RIGHT_FD_ALLOCATE | __WASI_RIGHT_FD_FILESTAT_SET_SIZE);
	const FileAccessMode accessMode
		= read && write ? FileAccessMode::readWrite
						: read ? FileAccessMode::readOnly
							   : write ? FileAccessMode::writeOnly : FileAccessMode::none;

	FileCreateMode createMode = FileCreateMode::openExisting;
	switch(openFlags & (__WASI_O_CREAT | __WASI_O_EXCL | __WASI_O_TRUNC))
	{
	case __WASI_O_CREAT | __WASI_O_EXCL: createMode = FileCreateMode::createNew; break;
	case __WASI_O_CREAT | __WASI_O_TRUNC: createMode = FileCreateMode::createAlways; break;
	case __WASI_O_CREAT: createMode = FileCreateMode::openAlways; break;
	case __WASI_O_TRUNC: createMode = FileCreateMode::truncateExisting; break;
	case 0:
		createMode = FileCreateMode::openExisting;
		break;

		// Undefined oflag combinations
	case __WASI_O_CREAT | __WASI_O_EXCL | __WASI_O_TRUNC:
	case __WASI_O_EXCL | __WASI_O_TRUNC:
	case __WASI_O_EXCL:
	default: return TRACE_SYSCALL_RETURN(__WASI_EINVAL);
	};

	if(openFlags & __WASI_O_CREAT) { requiredDirRights |= __WASI_RIGHT_PATH_CREATE_FILE; }
	if(openFlags & __WASI_O_TRUNC) { requiredDirRights |= __WASI_RIGHT_PATH_FILESTAT_SET_SIZE; }

	VFDFlags vfsVFDFlags = translateWASIVFDFlags(fdFlags, requiredDirInheritingRights);
	if(write && !(fdFlags & __WASI_FDFLAG_APPEND) && !(openFlags & __WASI_O_TRUNC))
	{ requiredDirInheritingRights |= __WASI_RIGHT_FD_SEEK; }

	std::string canonicalPath;
	const __wasi_errno_t pathError = validatePath(process,
												  dirFD,
												  lookupFlags,
												  requiredDirRights,
												  requiredDirInheritingRights,
												  pathAddress,
												  numPathBytes,
												  canonicalPath);
	if(pathError != __WASI_ESUCCESS) { return TRACE_SYSCALL_RETURN(pathError); }

	VFD* openedVFD = nullptr;
	const VFS::Result result
		= process->fileSystem->open(canonicalPath, accessMode, createMode, openedVFD, vfsVFDFlags);
	if(result != VFS::Result::success) { return TRACE_SYSCALL_RETURN(asWASIErrNo(result)); }

	__wasi_fd_t fd = process->fds.add(
		UINT32_MAX,
		FDE(openedVFD, requestedRights, requestedInheritingRights, std::move(canonicalPath)));
	if(fd == UINT32_MAX)
	{
		errorUnless(openedVFD->close() == VFS::Result::success);
		return TRACE_SYSCALL_RETURN(__WASI_EMFILE);
	}

	memoryRef<__wasi_fd_t>(process->memory, fdAddress) = fd;

	return TRACE_SYSCALL_RETURN(__WASI_ESUCCESS, " (%u)", fd);
}

static Uptr truncatingMemcpy(void* dest, const void* source, Uptr numSourceBytes, Uptr numDestBytes)
{
	Uptr numBytes = numSourceBytes;
	if(numBytes > numDestBytes) { numBytes = numDestBytes; }
	if(numBytes > 0) { memcpy(dest, source, numBytes); }
	return numBytes;
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wasiFile,
							   "fd_readdir",
							   __wasi_errno_return_t,
							   wasi_fd_readdir,
							   __wasi_fd_t dirFD,
							   WASIAddress bufferAddress,
							   WASIAddress numBufferBytes,
							   __wasi_dircookie_t firstCookie,
							   WASIAddress outNumBufferBytesUsedAddress)
{
	TRACE_SYSCALL("fd_readdir",
				  "(%u, " WASIADDRESS_FORMAT ", %u, 0x%" PRIx64 ", " WASIADDRESS_FORMAT ")",
				  dirFD,
				  bufferAddress,
				  numBufferBytes,
				  firstCookie,
				  outNumBufferBytesUsedAddress);

	Process* process = getProcessFromContextRuntimeData(contextRuntimeData);

	WASI::FDE* dirFDE = nullptr;
	const __wasi_errno_t fdError = validateFD(process, dirFD, __WASI_RIGHT_FD_READDIR, 0, dirFDE);
	if(fdError != __WASI_ESUCCESS) { return TRACE_SYSCALL_RETURN(fdError); }

	// If this is the first time readdir was called, open a DirEntStream for the FD.
	if(!dirFDE->dirEntStream)
	{
		if(firstCookie != __WASI_DIRCOOKIE_START) { return TRACE_SYSCALL_RETURN(__WASI_EINVAL); }

		const VFS::Result result = dirFDE->vfd->openDir(dirFDE->dirEntStream);
		if(result != VFS::Result::success) { return TRACE_SYSCALL_RETURN(asWASIErrNo(result)); }
	}
	else if(dirFDE->dirEntStream->tell() != firstCookie)
	{
		if(!dirFDE->dirEntStream->seek(firstCookie)) { return TRACE_SYSCALL_RETURN(__WASI_EINVAL); }
	}

	U8* buffer = memoryArrayPtr<U8>(process->memory, bufferAddress, numBufferBytes);
	Uptr numBufferBytesUsed = 0;

	while(numBufferBytesUsed < numBufferBytes)
	{
		DirEnt dirEnt;
		if(!dirFDE->dirEntStream->getNext(dirEnt)) { break; }

		errorUnless(dirEnt.name.size() <= UINT32_MAX);

		__wasi_dirent_t wasiDirEnt;
		wasiDirEnt.d_next = dirFDE->dirEntStream->tell();
		wasiDirEnt.d_ino = dirEnt.fileNumber;
		wasiDirEnt.d_namlen = U32(dirEnt.name.size());
		wasiDirEnt.d_type = asWASIFileType(dirEnt.type);

		numBufferBytesUsed += truncatingMemcpy(buffer + numBufferBytesUsed,
											   &wasiDirEnt,
											   sizeof(wasiDirEnt),
											   numBufferBytes - numBufferBytesUsed);

		numBufferBytesUsed += truncatingMemcpy(buffer + numBufferBytesUsed,
											   dirEnt.name.c_str(),
											   dirEnt.name.size(),
											   numBufferBytes - numBufferBytesUsed);
	};

	wavmAssert(numBufferBytesUsed <= numBufferBytes);
	memoryRef<WASIAddress>(process->memory, outNumBufferBytesUsedAddress)
		= WASIAddress(numBufferBytesUsed);

	return TRACE_SYSCALL_RETURN(
		__WASI_ESUCCESS, "(numBufferBytesUsed=%" PRIuPTR ")", numBufferBytesUsed);
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wasiFile,
							   "path_readlink",
							   __wasi_errno_return_t,
							   wasi_path_readlink,
							   __wasi_fd_t fd,
							   WASIAddress pathAddress,
							   WASIAddress numPathBytes,
							   WASIAddress bufferAddress,
							   WASIAddress numBufferBytes,
							   WASIAddress outNumBufferBytesUsedAddress)
{
	UNIMPLEMENTED_SYSCALL("path_readlink",
						  "(%u, " WASIADDRESS_FORMAT ", %u, " WASIADDRESS_FORMAT
						  ", %u, " WASIADDRESS_FORMAT ")",
						  fd,
						  pathAddress,
						  numPathBytes,
						  bufferAddress,
						  numBufferBytes,
						  outNumBufferBytesUsedAddress);
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wasiFile,
							   "path_rename",
							   __wasi_errno_return_t,
							   wasi_path_rename,
							   __wasi_fd_t oldFD,
							   WASIAddress oldPathAddress,
							   WASIAddress numOldPathBytes,
							   __wasi_fd_t newFD,
							   WASIAddress newPathAddress,
							   WASIAddress numNewPathBytes)
{
	UNIMPLEMENTED_SYSCALL("path_rename",
						  "(%u, " WASIADDRESS_FORMAT ", %u, %u, " WASIADDRESS_FORMAT ", %u)",
						  oldFD,
						  oldPathAddress,
						  numOldPathBytes,
						  newFD,
						  newPathAddress,
						  numNewPathBytes);
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wasiFile,
							   "fd_filestat_get",
							   __wasi_errno_return_t,
							   wasi_fd_filestat_get,
							   __wasi_fd_t fd,
							   WASIAddress filestatAddress)
{
	TRACE_SYSCALL("fd_filestat_get", "(%u, " WASIADDRESS_FORMAT ")", fd, filestatAddress);

	Process* process = getProcessFromContextRuntimeData(contextRuntimeData);

	WASI::FDE* fde = nullptr;
	const __wasi_errno_t fdError = validateFD(process, fd, __WASI_RIGHT_FD_FILESTAT_GET, 0, fde);
	if(fdError != __WASI_ESUCCESS) { return TRACE_SYSCALL_RETURN(fdError); }

	FileInfo fileInfo;
	const VFS::Result result = fde->vfd->getFileInfo(fileInfo);
	if(result != VFS::Result::success) { return TRACE_SYSCALL_RETURN(asWASIErrNo(result)); }

	__wasi_filestat_t& fileStat = memoryRef<__wasi_filestat_t>(process->memory, filestatAddress);

	fileStat.st_dev = fileInfo.deviceNumber;
	fileStat.st_ino = fileInfo.fileNumber;
	fileStat.st_filetype = asWASIFileType(fileInfo.type);
	fileStat.st_nlink = fileInfo.numLinks;
	fileStat.st_size = fileInfo.numBytes;
	fileStat.st_atim = __wasi_timestamp_t(fileInfo.lastAccessTime);
	fileStat.st_mtim = __wasi_timestamp_t(fileInfo.lastWriteTime);
	fileStat.st_ctim = __wasi_timestamp_t(fileInfo.creationTime);

	return TRACE_SYSCALL_RETURN(__WASI_ESUCCESS);
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wasiFile,
							   "fd_filestat_set_times",
							   __wasi_errno_return_t,
							   wasi_fd_filestat_set_times,
							   __wasi_fd_t fd,
							   __wasi_timestamp_t lastAccessTime,
							   __wasi_timestamp_t lastWriteTime,
							   __wasi_fstflags_t flags)
{
	TRACE_SYSCALL("fd_filestat_set_times",
				  "(%u, %" PRIu64 ", %" PRIu64 ", 0x%04x)",
				  fd,
				  lastAccessTime,
				  lastWriteTime,
				  flags);

	Process* process = getProcessFromContextRuntimeData(contextRuntimeData);

	WASI::FDE* fde = nullptr;
	const __wasi_errno_t fdError
		= validateFD(process, fd, __WASI_RIGHT_FD_FILESTAT_SET_TIMES, 0, fde);
	if(fdError != __WASI_ESUCCESS) { return TRACE_SYSCALL_RETURN(fdError); }

	I128 now = Platform::getRealtimeClock();

	bool setLastAccessTime = false;
	I128 lastAccessTimeI128;
	if(flags & __WASI_FILESTAT_SET_ATIM)
	{
		lastAccessTimeI128 = I128(lastAccessTime);
		setLastAccessTime = true;
	}
	else if(flags & __WASI_FILESTAT_SET_ATIM_NOW)
	{
		lastAccessTimeI128 = now;
		setLastAccessTime = true;
	}

	bool setLastWriteTime = false;
	I128 lastWriteTimeI128;
	if(flags & __WASI_FILESTAT_SET_MTIM)
	{
		lastWriteTimeI128 = I128(lastWriteTime);
		setLastWriteTime = true;
	}
	else if(flags & __WASI_FILESTAT_SET_MTIM_NOW)
	{
		lastWriteTimeI128 = now;
		setLastWriteTime = true;
	}

	const VFS::Result result = fde->vfd->setFileTimes(
		setLastAccessTime, lastAccessTimeI128, setLastWriteTime, lastWriteTimeI128);

	return TRACE_SYSCALL_RETURN(asWASIErrNo(result));
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wasiFile,
							   "fd_filestat_set_size",
							   __wasi_errno_return_t,
							   wasi_fd_filestat_set_size,
							   __wasi_fd_t fd,
							   __wasi_filesize_t numBytes)
{
	TRACE_SYSCALL("fd_filestat_set_size", "(%u, %" PRIu64 ")", fd, numBytes);

	Process* process = getProcessFromContextRuntimeData(contextRuntimeData);

	WASI::FDE* fde = nullptr;
	const __wasi_errno_t fdError
		= validateFD(process, fd, __WASI_RIGHT_FD_FILESTAT_SET_SIZE, 0, fde);
	if(fdError != __WASI_ESUCCESS) { return TRACE_SYSCALL_RETURN(fdError); }

	return TRACE_SYSCALL_RETURN(asWASIErrNo(fde->vfd->setFileSize(numBytes)));
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wasiFile,
							   "path_filestat_get",
							   __wasi_errno_return_t,
							   wasi_path_filestat_get,
							   __wasi_fd_t dirFD,
							   __wasi_lookupflags_t lookupFlags,
							   WASIAddress pathAddress,
							   WASIAddress numPathBytes,
							   WASIAddress filestatAddress)
{
	TRACE_SYSCALL("path_filestat_get",
				  "(%u, 0x%08x, " WASIADDRESS_FORMAT ", %u, " WASIADDRESS_FORMAT ")",
				  dirFD,
				  lookupFlags,
				  pathAddress,
				  numPathBytes,
				  filestatAddress);

	Process* process = getProcessFromContextRuntimeData(contextRuntimeData);

	std::string canonicalPath;
	const __wasi_errno_t pathError = validatePath(process,
												  dirFD,
												  lookupFlags,
												  __WASI_RIGHT_PATH_FILESTAT_GET,
												  0,
												  pathAddress,
												  numPathBytes,
												  absolutePath);
	if(pathError != __WASI_ESUCCESS) { return TRACE_SYSCALL_RETURN(pathError); }

	FileInfo fileInfo;
	const VFS::Result result = process->fileSystem->getFileInfo(absolutePath, fileInfo);
	if(result != VFS::Result::success) { return TRACE_SYSCALL_RETURN(asWASIErrNo(result)); }

	__wasi_filestat_t& fileStat = memoryRef<__wasi_filestat_t>(process->memory, filestatAddress);

	fileStat.st_dev = fileInfo.deviceNumber;
	fileStat.st_ino = fileInfo.fileNumber;
	fileStat.st_filetype = asWASIFileType(fileInfo.type);
	fileStat.st_nlink = fileInfo.numLinks;
	fileStat.st_size = fileInfo.numBytes;
	fileStat.st_atim = __wasi_timestamp_t(fileInfo.lastAccessTime);
	fileStat.st_mtim = __wasi_timestamp_t(fileInfo.lastWriteTime);
	fileStat.st_ctim = __wasi_timestamp_t(fileInfo.creationTime);

	return TRACE_SYSCALL_RETURN(__WASI_ESUCCESS);
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wasiFile,
							   "path_filestat_set_times",
							   __wasi_errno_return_t,
							   wasi_path_filestat_set_times,
							   __wasi_fd_t dirFD,
							   __wasi_lookupflags_t lookupFlags,
							   WASIAddress pathAddress,
							   WASIAddress numPathBytes,
							   __wasi_timestamp_t lastAccessTime,
							   __wasi_timestamp_t lastWriteTime,
							   __wasi_fstflags_t flags)
{
	TRACE_SYSCALL("path_filestat_set_times",
				  "(%u, 0x%08x, " WASIADDRESS_FORMAT ", %u, %" PRIu64 ", %" PRIu64 ", 0x%04x)",
				  dirFD,
				  lookupFlags,
				  pathAddress,
				  numPathBytes,
				  lastAccessTime,
				  lastWriteTime,
				  flags);

	Process* process = getProcessFromContextRuntimeData(contextRuntimeData);

	std::string canonicalPath;
	const __wasi_errno_t pathError = validatePath(process,
												  dirFD,
												  lookupFlags,
												  __WASI_RIGHT_PATH_FILESTAT_SET_TIMES,
												  0,
												  pathAddress,
												  numPathBytes,
												  canonicalPath);
	if(pathError != __WASI_ESUCCESS) { return TRACE_SYSCALL_RETURN(pathError); }

	I128 now = Platform::getRealtimeClock();

	bool setLastAccessTime = false;
	I128 lastAccessTimeI128;
	if(flags & __WASI_FILESTAT_SET_ATIM)
	{
		lastAccessTimeI128 = I128(lastAccessTime);
		setLastAccessTime = true;
	}
	else if(flags & __WASI_FILESTAT_SET_ATIM_NOW)
	{
		lastAccessTimeI128 = now;
		setLastAccessTime = true;
	}

	bool setLastWriteTime = false;
	I128 lastWriteTimeI128;
	if(flags & __WASI_FILESTAT_SET_MTIM)
	{
		lastWriteTimeI128 = I128(lastWriteTime);
		setLastWriteTime = true;
	}
	else if(flags & __WASI_FILESTAT_SET_MTIM_NOW)
	{
		lastWriteTimeI128 = now;
		setLastWriteTime = true;
	}

	const VFS::Result result = process->fileSystem->setFileTimes(
		canonicalPath, setLastAccessTime, lastAccessTimeI128, setLastWriteTime, lastWriteTimeI128);
	return TRACE_SYSCALL_RETURN(asWASIErrNo(result));
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wasiFile,
							   "path_symlink",
							   __wasi_errno_return_t,
							   wasi_path_symlink,
							   WASIAddress oldPathAddress,
							   WASIAddress numOldPathBytes,
							   __wasi_fd_t fd,
							   WASIAddress newPathAddress,
							   WASIAddress numNewPathBytes)
{
	UNIMPLEMENTED_SYSCALL("path_symlink",
						  "(" WASIADDRESS_FORMAT ", %u, %u, " WASIADDRESS_FORMAT ", %u)",
						  oldPathAddress,
						  numOldPathBytes,
						  fd,
						  newPathAddress,
						  numNewPathBytes);
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wasiFile,
							   "path_unlink_file",
							   __wasi_errno_return_t,
							   wasi_path_unlink_file,
							   __wasi_fd_t dirFD,
							   WASIAddress pathAddress,
							   WASIAddress numPathBytes)
{
	TRACE_SYSCALL(
		"path_unlink_file", "(%u, " WASIADDRESS_FORMAT ", %u)", dirFD, pathAddress, numPathBytes);

	Process* process = getProcessFromContextRuntimeData(contextRuntimeData);

	std::string canonicalPath;
	const __wasi_errno_t pathError = validatePath(process,
												  dirFD,
												  0,
												  __WASI_RIGHT_PATH_UNLINK_FILE,
												  0,
												  pathAddress,
												  numPathBytes,
												  canonicalPath);
	if(pathError != __WASI_ESUCCESS) { return TRACE_SYSCALL_RETURN(pathError); }

	if(!process->fileSystem) { return TRACE_SYSCALL_RETURN(__WASI_ENOTCAPABLE); }

	Result result = process->fileSystem->unlinkFile(canonicalPath);
	return TRACE_SYSCALL_RETURN(result == VFS::Result::isDirectory ? __WASI_EPERM
																   : asWASIErrNo(result));
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wasiFile,
							   "path_remove_directory",
							   __wasi_errno_return_t,
							   wasi_path_remove_directory,
							   __wasi_fd_t dirFD,
							   WASIAddress pathAddress,
							   WASIAddress numPathBytes)
{
	TRACE_SYSCALL("path_remove_directory",
				  "(%u, " WASIADDRESS_FORMAT ", %u)",
				  dirFD,
				  pathAddress,
				  numPathBytes);

	Process* process = getProcessFromContextRuntimeData(contextRuntimeData);

	std::string canonicalPath;
	const __wasi_errno_t pathError = validatePath(process,
												  dirFD,
												  0,
												  __WASI_RIGHT_PATH_REMOVE_DIRECTORY,
												  0,
												  pathAddress,
												  numPathBytes,
												  canonicalPath);
	if(pathError != __WASI_ESUCCESS) { return TRACE_SYSCALL_RETURN(pathError); }

	if(!process->fileSystem) { return TRACE_SYSCALL_RETURN(__WASI_ENOTCAPABLE); }

	const VFS::Result result = process->fileSystem->removeDir(canonicalPath);
	return TRACE_SYSCALL_RETURN(asWASIErrNo(result));
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wasiFile,
							   "path_create_directory",
							   __wasi_errno_return_t,
							   wasi_path_create_directory,
							   __wasi_fd_t dirFD,
							   WASIAddress pathAddress,
							   WASIAddress numPathBytes)
{
	TRACE_SYSCALL("path_create_directory",
				  "(%u, " WASIADDRESS_FORMAT ", %u)",
				  dirFD,
				  pathAddress,
				  numPathBytes);

	Process* process = getProcessFromContextRuntimeData(contextRuntimeData);

	std::string canonicalPath;
	const __wasi_errno_t pathError = validatePath(process,
												  dirFD,
												  0,
												  __WASI_RIGHT_PATH_CREATE_DIRECTORY,
												  0,
												  pathAddress,
												  numPathBytes,
												  canonicalPath);
	if(pathError != __WASI_ESUCCESS) { return TRACE_SYSCALL_RETURN(pathError); }

	const VFS::Result result = process->fileSystem->createDir(canonicalPath);
	return TRACE_SYSCALL_RETURN(asWASIErrNo(result));
}
