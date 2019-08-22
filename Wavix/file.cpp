#include "WAVM/Platform/File.h"
#include <string.h>
#include <string>
#include <utility>
#include <vector>
#include "WAVM/Inline/Assert.h"
#include "WAVM/Inline/BasicTypes.h"
#include "WAVM/Inline/IndexMap.h"
#include "WAVM/Inline/Lock.h"
#include "WAVM/Inline/Unicode.h"
#include "WAVM/Logging/Logging.h"
#include "WAVM/Runtime/Intrinsics.h"
#include "WAVM/Runtime/Runtime.h"
#include "errno.h"
#include "file.h"
#include "process.h"
#include "wavix.h"

using namespace WAVM::Runtime;
using namespace Wavix;

namespace Wavix {
	WAVM_DEFINE_INTRINSIC_MODULE(wavixFile);
}

static bool validateFD(I32 index)
{
	return index >= 0 && index <= currentProcess->files.getMaxIndex()
		   && currentProcess->files.contains(Uptr(index));
}

struct Path
{
	enum class Base
	{
		cwd,
		home,
		root,
	};

	Base base;
	std::vector<std::string> components;
};

static bool parsePath(const std::string& pathString, Path& outPath)
{
	outPath.components.clear();
	if(pathString.size() == 0) { return false; }

	const U8* nextChar = (const U8*)pathString.c_str();
	const U8* endChar = nextChar + pathString.size();

	switch(*nextChar)
	{
	case '/':
		outPath.base = Path::Base::root;
		++nextChar;
		break;
	case '~':
		outPath.base = Path::Base::home;
		++nextChar;
		break;
	default: outPath.base = Path::Base::cwd; break;
	};

	std::string nextComponent;
	while(nextChar != endChar)
	{
		switch(*nextChar)
		{
		case '/': {
			++nextChar;
			if(nextComponent.size()) { outPath.components.emplace_back(std::move(nextComponent)); }
			break;
		}
		default: {
			U32 codePoint = 0;
			const U8* codePointStart = nextChar;
			if(!Unicode::decodeUTF8CodePoint(nextChar, endChar, codePoint)) { return false; }
			while(codePointStart < nextChar) { nextComponent += *codePointStart++; }
			break;
		}
		};
	};

	if(nextComponent.size()) { outPath.components.emplace_back(std::move(nextComponent)); }

	return true;
}

#define PATH_SEPARATOR '/'

static std::string resolvePath(const std::string& cwd, const std::string& home, const Path& path)
{
	std::string result;
	switch(path.base)
	{
	case Path::Base::cwd: result = cwd; break;
	case Path::Base::home: result = home; break;
	default: break;
	};

	for(const std::string& component : path.components)
	{
		result += PATH_SEPARATOR;
		result += component;
	}

	return result;
}

namespace OpenFlags {
	enum
	{
		readOnly = 0x0000,
		writeOnly = 0x0001,
		readWrite = 0x0002,
		accessModeMask = 0x0003,

		create = 0x0040,
		exclusive = 0x0080,
		truncate = 0x0200,
		createModeMask = create | exclusive | truncate,

		// noCTTY = 0x0100,
		// append = 0x0400,
		// nonBlocking = 0x0800,
	};
};

WAVM_DEFINE_INTRINSIC_FUNCTION(wavixFile,
							   "__syscall_open",
							   I32,
							   __syscall_open,
							   U32 pathAddress,
							   U32 flags,
							   U32 mode)
{
	Memory* memory = currentThread->process->memory;
	std::string pathString = readUserString(memory, pathAddress);

	traceSyscallf("open", "(%08x=>\"%s\", 0x%x, %u)", pathAddress, pathString.c_str(), flags, mode);

	Path path;
	if(!parsePath(pathString, path))
	{
		traceSyscallReturnf("open", "ENOENT (couldn't parse path)");
		return -ErrNo::enoent;
	}

	std::string cwd;
	{
		Lock<Platform::Mutex> cwdLock(currentProcess->cwdMutex);
		cwd = currentProcess->cwd;
	}
	pathString = sysroot + resolvePath(cwd, "/home", path);

	// Validate and interpret the access mode.
	VFS::FileAccessMode platformAccessMode;
	const U32 accessMode = flags & OpenFlags::accessModeMask;
	switch(accessMode)
	{
	case OpenFlags::readOnly: platformAccessMode = VFS::FileAccessMode::readOnly; break;
	case OpenFlags::writeOnly: platformAccessMode = VFS::FileAccessMode::writeOnly; break;
	case OpenFlags::readWrite: platformAccessMode = VFS::FileAccessMode::readWrite; break;
	default: traceSyscallReturnf("open", "EINVAL (invalid flags)"); return -ErrNo::einval;
	};

	VFS::FileCreateMode platformCreateMode;
	if(flags & OpenFlags::create)
	{
		if(flags & OpenFlags::exclusive) { platformCreateMode = VFS::FileCreateMode::createNew; }
		else if(flags & OpenFlags::truncate)
		{
			platformCreateMode = VFS::FileCreateMode::createAlways;
		}
		else
		{
			platformCreateMode = VFS::FileCreateMode::openAlways;
		}
	}
	else
	{
		if(flags & OpenFlags::truncate)
		{ platformCreateMode = VFS::FileCreateMode::truncateExisting; }
		else
		{
			platformCreateMode = VFS::FileCreateMode::openExisting;
		}
	}

	VFS::VFD* vfd = nullptr;
	VFS::Result openResult
		= Platform::getHostFS().open(pathString, platformAccessMode, platformCreateMode, vfd);
	if(openResult != VFS::Result::success)
	{
		traceSyscallReturnf("open", "EACCESS (Platform::openFile failed)");
		return -ErrNo::eacces;
	}

	Lock<Platform::Mutex> fileLock(currentProcess->filesMutex);
	I32 fd = currentProcess->files.add(-1, vfd);
	if(fd == -1)
	{
		WAVM_ERROR_UNLESS(vfd->close() == VFS::Result::success);
		traceSyscallReturnf("open", "EMFILE (exhausted fd index space)");
		return -ErrNo::emfile;
	}
	traceSyscallReturnf("open", "%i", fd);
	return fd;
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wavixFile,
							   "__syscall_openat",
							   I32,
							   __syscall_openat,
							   I32 dirfd,
							   U32 pathAddress,
							   U32 flags,
							   U32 mode)
{
	traceSyscallf("openat", "");
	throwException(ExceptionTypes::calledUnimplementedIntrinsic);
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wavixFile,
							   "__syscall_creat",
							   I32,
							   __syscall_creat,
							   U32 pathAddress,
							   U32 mode)
{
	traceSyscallf("creat", "");
	throwException(ExceptionTypes::calledUnimplementedIntrinsic);
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wavixFile, "__syscall_close", I32, __syscall_close, I32 fd)
{
	traceSyscallf("close", "");

	Lock<Platform::Mutex> fileLock(currentProcess->filesMutex);

	if(!validateFD(fd)) { return -1; }

	VFS::VFD* vfd = currentProcess->files[fd];
	currentProcess->files.removeOrFail(fd);

	if(vfd->close() == VFS::Result::success) { return 0; }
	else
	{
		return -1;
	}
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wavixFile,
							   "__syscall_llseek",
							   I32,
							   __syscall_llseek,
							   I32 fd,
							   U32 offsetHigh,
							   U32 offsetLow,
							   U32 resultAddress,
							   U32 whence)
{
	Memory* memory = currentThread->process->memory;

	traceSyscallf("llseek", "");

	const I64 offset = I64(U64(offsetHigh) << 32) | I64(offsetLow);

	VFS::SeekOrigin seekOrigin;
	switch(whence)
	{
	case 0: seekOrigin = VFS::SeekOrigin::begin; break;
	case 1: seekOrigin = VFS::SeekOrigin::cur; break;
	case 2: seekOrigin = VFS::SeekOrigin::end; break;
	default: return -1;
	};

	Lock<Platform::Mutex> fileLock(currentProcess->filesMutex);

	if(!validateFD(fd)) { return -1; }

	VFS::VFD* vfd = currentProcess->files[fd];

	VFS::Result result = vfd->seek(offset, seekOrigin, &memoryRef<U64>(memory, resultAddress));
	if(result != VFS::Result::success) { return -1; }

	return 0;
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wavixFile,
							   "__syscall_read",
							   I32,
							   __syscall_read,
							   I32 fd,
							   U32 bufferAddress,
							   U32 numBytes)
{
	Memory* memory = currentThread->process->memory;

	traceSyscallf("read", "(%i,0x%08x,%u)", fd, bufferAddress, numBytes);

	Lock<Platform::Mutex> fileLock(currentProcess->filesMutex);

	if(!validateFD(fd)) { return -ErrNo::ebadf; }

	VFS::VFD* vfd = currentProcess->files[fd];
	if(!vfd) { return -ErrNo::ebadf; }

	U8* buffer = memoryArrayPtr<U8>(memory, bufferAddress, numBytes);

	Uptr numReadBytes = 0;
	VFS::Result result = vfd->read(buffer, numBytes, &numReadBytes);
	if(result != VFS::Result::success) { return -1; }

	return coerce32bitAddressSigned(numReadBytes);
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wavixFile,
							   "__syscall_write",
							   I32,
							   __syscall_write,
							   I32 fd,
							   U32 bufferAddress,
							   U32 numBytes)
{
	Memory* memory = currentThread->process->memory;

	traceSyscallf("write", "(%i,0x%08x,%u)", fd, bufferAddress, numBytes);

	Lock<Platform::Mutex> fileLock(currentProcess->filesMutex);

	if(!validateFD(fd)) { return -ErrNo::ebadf; }

	VFS::VFD* vfd = currentProcess->files[fd];
	if(!vfd) { return -ErrNo::ebadf; }

	U8* buffer = memoryArrayPtr<U8>(memory, bufferAddress, numBytes);

	Uptr numWrittenBytes = 0;
	VFS::Result result = vfd->write(buffer, numBytes, &numWrittenBytes);
	if(result != VFS::Result::success) { return -ErrNo::einval; }

	return coerce32bitAddressSigned(numWrittenBytes);
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wavixFile,
							   "__syscall_readv",
							   I32,
							   __syscall_readv,
							   I32 fd,
							   U32 iosAddress,
							   U32 numIos)
{
	Memory* memory = currentThread->process->memory;

	traceSyscallf("readv", "");

	struct IoVec
	{
		U32 address;
		U32 numBytes;
	};

	Lock<Platform::Mutex> fileLock(currentProcess->filesMutex);

	if(!validateFD(fd)) { return -1; }

	VFS::VFD* vfd = currentProcess->files[fd];
	if(!vfd) { return -ErrNo::ebadf; }

	if(isTracingSyscalls) { Log::printf(Log::debug, "IOVs:\n"); }

	const IoVec* ios = memoryArrayPtr<const IoVec>(memory, iosAddress, numIos);
	Uptr numReadBytes = 0;
	for(U32 ioIndex = 0; ioIndex < U32(numIos); ++ioIndex)
	{
		const IoVec io = ios[ioIndex];

		if(isTracingSyscalls)
		{ Log::printf(Log::debug, "  [%u] 0x%x, %u bytes\n", ioIndex, io.address, io.numBytes); }

		if(io.numBytes)
		{
			U8* ioData = memoryArrayPtr<U8>(memory, io.address, io.numBytes);
			Uptr ioNumReadBytes = 0;
			VFS::Result readResult = vfd->read(ioData, io.numBytes, &ioNumReadBytes);
			numReadBytes += ioNumReadBytes;
			if(readResult != VFS::Result::success || ioNumReadBytes != io.numBytes) { break; }
		}
	}

	return coerce32bitAddressSigned(numReadBytes);
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wavixFile,
							   "__syscall_writev",
							   I32,
							   __syscall_writev,
							   I32 fd,
							   U32 iosAddress,
							   U32 numIOs)
{
	Memory* memory = currentThread->process->memory;

	traceSyscallf("writev", "(%i, 0x%x, %u)", fd, iosAddress, numIOs);

	struct IoVec
	{
		U32 address;
		U32 numBytes;
	};

	Lock<Platform::Mutex> fileLock(currentProcess->filesMutex);

	if(!validateFD(fd)) { return -1; }

	VFS::VFD* vfd = currentProcess->files[fd];
	if(!vfd) { return -ErrNo::ebadf; }

	if(isTracingSyscalls) { Log::printf(Log::debug, "IOVs:\n"); }

	const IoVec* ios = memoryArrayPtr<const IoVec>(memory, iosAddress, numIOs);
	Uptr numWrittenBytes = 0;
	for(U32 ioIndex = 0; ioIndex < U32(numIOs); ++ioIndex)
	{
		const IoVec io = ios[ioIndex];

		if(isTracingSyscalls)
		{ Log::printf(Log::debug, "  [%u] 0x%x, %u bytes\n", ioIndex, io.address, io.numBytes); }

		if(io.numBytes)
		{
			const U8* ioData = memoryArrayPtr<U8>(memory, io.address, io.numBytes);
			Uptr ioNumWrittenBytes = 0;
			VFS::Result writeResult = vfd->write(ioData, io.numBytes, &ioNumWrittenBytes);
			numWrittenBytes += ioNumWrittenBytes;
			if(writeResult != VFS::Result::success || ioNumWrittenBytes != io.numBytes) { break; }
		}
	}

	return coerce32bitAddressSigned(numWrittenBytes);
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wavixFile, "__syscall_fsync", I32, __syscall_fsync, I32 fd)
{
	traceSyscallf("fsync", "");

	Lock<Platform::Mutex> fileLock(currentProcess->filesMutex);

	if(!validateFD(fd)) { return -1; }

	VFS::VFD* vfd = currentProcess->files[fd];
	if(!vfd) { return -ErrNo::ebadf; }

	VFS::Result result = vfd->sync(VFS::SyncType::contentsAndMetadata);
	if(result != VFS::Result::success) { return -1; }

	return 0;
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wavixFile, "__syscall_fdatasync", I32, __syscall_fdatasync, I32 fd)
{
	traceSyscallf("fdatasync", "");

	Lock<Platform::Mutex> fileLock(currentProcess->filesMutex);

	if(!validateFD(fd)) { return -1; }

	VFS::VFD* vfd = currentProcess->files[fd];
	if(!vfd) { return -ErrNo::ebadf; }

	VFS::Result result = vfd->sync(VFS::SyncType::contents);
	if(result != VFS::Result::success) { return -1; }

	return 0;
}

#define WAVIX_F_DUPFD 0
#define WAVIX_F_GETFD 1
#define WAVIX_F_SETFD 2
#define WAVIX_F_GETFL 3
#define WAVIX_F_SETFL 4
#define WAVIX_F_GETLK 5
#define WAVIX_F_SETLK 6
#define WAVIX_F_SETLKW 7
#define WAVIX_F_GETOWN 9
#define WAVIX_F_SETOWN 8
#define WAVIX_FD_CLOEXEC 1
#define WAVIX_F_RDLCK 0
#define WAVIX_F_UNLCK 2
#define WAVIX_F_WRLCK 1
#define WAVIX_SEEK_SET 0
#define WAVIX_SEEK_CUR 1
#define WAVIX_SEEK_END 2
#define WAVIX_O_CREAT 0100
#define WAVIX_O_EXCL 0200
#define WAVIX_O_NOCTTY 0400
#define WAVIX_O_TRUNC 01000
#define WAVIX_O_APPEND 02000
#define WAVIX_O_DSYNC 010000
#define WAVIX_O_NONBLOCK 04000
#define WAVIX_O_RSYNC 04010000
#define WAVIX_O_SYNC 04010000
#define WAVIX_O_ACCMODE 010000030
#define WAVIX_O_RDONLY 00
#define WAVIX_O_RDWR 02
#define WAVIX_O_WRONLY 01

WAVM_DEFINE_INTRINSIC_FUNCTION(wavixFile,
							   "__syscall_fcntl64",
							   I32,
							   __syscall_fcntl64,
							   I32 fd,
							   U32 cmd,
							   U32 arg)
{
	traceSyscallf("fnctl64", "(%i,%i,%i)", fd, cmd, arg);
	switch(cmd)
	{
	case WAVIX_F_GETFL: return 0;
	case WAVIX_F_SETFL: return 0;
	case WAVIX_F_SETFD: return 0;
	default: throwException(ExceptionTypes::calledUnimplementedIntrinsic);
	}
}

typedef I32 wavix_uid_t;
typedef I32 wavix_gid_t;

typedef U32 wavix_mode_t;
typedef U32 wavix_nlink_t;
typedef I64 wavix_off_t;
typedef U64 wavix_ino_t;
typedef U64 wavix_dev_t;
typedef I32 wavix_blksize_t;
typedef I64 wavix_blkcnt_t;
typedef U64 wavix_fsblkcnt_t;
typedef U64 wavix_fsfilcnt_t;

struct wavix_stat
{
	wavix_dev_t st_dev;
	I32 __st_dev_padding;
	I32 __st_ino_truncated;
	wavix_mode_t st_mode;
	wavix_nlink_t st_nlink;
	wavix_uid_t st_uid;
	wavix_gid_t st_gid;
	wavix_dev_t st_rdev;
	I32 __st_rdev_padding;
	wavix_off_t st_size;
	wavix_blksize_t st_blksize;
	wavix_blkcnt_t st_blocks;
	wavix_timespec st_atim;
	wavix_timespec st_mtim;
	wavix_timespec st_ctim;
	wavix_ino_t st_ino;
};

WAVM_DEFINE_INTRINSIC_FUNCTION(wavixFile,
							   "__syscall_stat64",
							   I32,
							   __syscall_stat64,
							   U32 pathAddress,
							   U32 resultAddress)
{
	Memory* memory = currentThread->process->memory;

	std::string pathString = readUserString(memory, pathAddress);

	traceSyscallf("stat64", "(\"%s\",0x%08x)", pathString.c_str(), resultAddress);

	Path path;
	if(!parsePath(pathString, path)) { return -1; }

	unwindSignalsAsExceptions([=] {
		wavix_stat& result = memoryRef<wavix_stat>(memory, resultAddress);
		memset(&result, 0, sizeof(wavix_stat));
	});

	return 0;
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wavixFile,
							   "__syscall_lstat64",
							   I32,
							   __syscall_lstat64,
							   U32 pathAddress,
							   U32 resultAddress)
{
	Memory* memory = currentThread->process->memory;

	std::string pathString = readUserString(memory, pathAddress);
	Path path;

	traceSyscallf("lstat64", "(\"%s\",0x%08x)", pathString.c_str(), resultAddress);

	if(!parsePath(pathString, path)) { return -1; }

	unwindSignalsAsExceptions([=] {
		wavix_stat& result = memoryRef<wavix_stat>(memory, resultAddress);
		memset(&result, 0, sizeof(wavix_stat));
	});

	return 0;
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wavixFile,
							   "__syscall_fstat64",
							   I32,
							   __syscall_fstat64,
							   I32 fd,
							   U32 resultAddress)
{
	Memory* memory = currentThread->process->memory;

	traceSyscallf("fstat64", "(%i,0x%08x)", fd, resultAddress);

	wavix_stat& result = memoryRef<wavix_stat>(memory, resultAddress);
	memset(&result, 0, sizeof(wavix_stat));

	return 0;
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wavixFile,
							   "__syscall_faccessat",
							   I32,
							   __syscall_faccessat,
							   I32 dirfd,
							   U32 pathAddress,
							   U32 mode,
							   U32 flags)
{
	Memory* memory = currentThread->process->memory;

	std::string pathString = readUserString(memory, pathAddress);
	Path path;

	traceSyscallf("faccessat", "(%i,\"%s\",%u,%u)", dirfd, pathString.c_str(), mode, flags);

	if(!parsePath(pathString, path)) { return -1; }

	return -1;
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wavixFile, "__syscall_unlink", I32, __syscall_unlink, I32 a)
{
	traceSyscallf("unlink", "(%i)", a);
	throwException(ExceptionTypes::calledUnimplementedIntrinsic);
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wavixFile, "__syscall_chdir", I32, __syscall_chdir, U32 pathAddress)
{
	Memory* memory = currentThread->process->memory;

	std::string pathString = readUserString(memory, pathAddress);

	traceSyscallf("chdir", "(\"%s\")", pathString.c_str());

	Path path;
	if(!parsePath(pathString, path)) { return -1; }

	Lock<Platform::Mutex> cwdLock(currentProcess->cwdMutex);
	currentProcess->cwd = resolvePath(currentProcess->cwd, "/home", path);

	return 0;
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wavixFile, "__syscall_access", I32, __syscall_access, I32 a, I32 b)
{
	traceSyscallf("access", "(%i,%i)", a, b);
	throwException(ExceptionTypes::calledUnimplementedIntrinsic);
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wavixFile, "__syscall_dup", I32, __syscall_dup, I32 a)
{
	traceSyscallf("dup", "(%i)", a);
	throwException(ExceptionTypes::calledUnimplementedIntrinsic);
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wavixFile, "__syscall_dup2", I32, __syscall_dup2, I32 a, I32 b)
{
	traceSyscallf("dup2", "(%i,%i)", a, b);
	throwException(ExceptionTypes::calledUnimplementedIntrinsic);
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wavixFile,
							   "__syscall_readlink",
							   I32,
							   __syscall_readlink,
							   U32 pathAddress,
							   U32 bufferAddress,
							   U32 numBufferBytes)
{
	Memory* memory = currentThread->process->memory;

	std::string pathString = readUserString(memory, pathAddress);

	traceSyscallf("readlink", "(%s,0x%08x,%u)", pathString.c_str(), bufferAddress, numBufferBytes);

	Path path;
	if(!parsePath(pathString, path)) { return -1; }

	// throwException(ExceptionTypes::calledUnimplementedIntrinsic);
	return -1;
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wavixFile, "__syscall_umask", I32, __syscall_umask, I32 a)
{
	traceSyscallf("umask", "(%i)", a);
	throwException(ExceptionTypes::calledUnimplementedIntrinsic);
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wavixFile, "__syscall_rename", I32, __syscall_rename, I32 a, I32 b)
{
	traceSyscallf("rename", "(%i,%i)", a, b);
	throwException(ExceptionTypes::calledUnimplementedIntrinsic);
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wavixFile,
							   "__syscall_chown32",
							   I32,
							   __syscall_chown32,
							   I32 a,
							   I32 b,
							   I32 c)
{
	traceSyscallf("chown32", "(%i,%i,%i)", a, b, c);
	throwException(ExceptionTypes::calledUnimplementedIntrinsic);
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wavixFile,
							   "__syscall_getdents64",
							   I32,
							   __syscall_getdents64,
							   I32 a,
							   I32 b,
							   I32 c)
{
	traceSyscallf("getdents64", "(%i,%i,%i)", a, b, c);
	throwException(ExceptionTypes::calledUnimplementedIntrinsic);
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wavixFile, "__syscall_pipe", I32, __syscall_pipe, I32 a)
{
	traceSyscallf("pipe", "(%i)", a);
	throwException(ExceptionTypes::calledUnimplementedIntrinsic);
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wavixFile,
							   "__syscall_poll",
							   I32,
							   __syscall_poll,
							   I32 a,
							   I32 b,
							   I32 c)
{
	traceSyscallf("poll", "(%i,%i,%i)", a, b, c);
	throwException(ExceptionTypes::calledUnimplementedIntrinsic);
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wavixFile,
							   "__syscall_pselect6",
							   I32,
							   __syscall_pselect6,
							   I32 a,
							   I32 b,
							   I32 c,
							   I32 d,
							   I32 e,
							   I32 f)
{
	traceSyscallf("pselect", "(%i,0x%08x,0x%08x,0x%08x,0x%08x,0x%08x)", a, b, c, d, e, f);
	// throwException(ExceptionTypes::calledUnimplementedIntrinsic);
	return 0;
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wavixFile,
							   "__syscall__newselect",
							   I32,
							   __syscall__newselect,
							   I32 a,
							   I32 b,
							   I32 c,
							   I32 d,
							   I32 e)
{
	traceSyscallf("_newselect", "(%i,0x%08x,0x%08x,0x%08x,0x%08x)", a, b, c, d, e);
	// throwException(ExceptionTypes::calledUnimplementedIntrinsic);
	return 0;
}

#define TIOCGWINSZ 0x5413

WAVM_DEFINE_INTRINSIC_FUNCTION(wavixFile,
							   "__syscall_ioctl",
							   I32,
							   __syscall_ioctl,
							   I32 fd,
							   U32 request,
							   U32 arg0,
							   U32 arg1,
							   U32 arg2,
							   U32 arg3)
{
	Memory* memory = currentThread->process->memory;

	traceSyscallf("ioctl", "(%i,%i)", fd, request);
	switch(request)
	{
	case TIOCGWINSZ: {
		struct WinSize
		{
			unsigned short ws_row;    /* rows, in characters */
			unsigned short ws_col;    /* columns, in characters */
			unsigned short ws_xpixel; /* horizontal size, pixels */
			unsigned short ws_ypixel; /* vertical size, pixels */
		};

		WinSize& winSize = memoryRef<WinSize>(memory, arg0);
		winSize.ws_row = 43;
		winSize.ws_col = 80;
		winSize.ws_xpixel = 800;
		winSize.ws_ypixel = 600;
		return 0;
	}
	default: return -ErrNo::einval;
	};
}
