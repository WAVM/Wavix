#include "wavix.h"

#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <atomic>
#include <memory>

#include "WAVM/Inline/BasicTypes.h"
#include "WAVM/Inline/Errors.h"
#include "WAVM/Inline/Lock.h"
#include "WAVM/Platform/Defines.h"
#include "WAVM/Platform/File.h"
#include "WAVM/Platform/Intrinsic.h"
#include "WAVM/Platform/Mutex.h"
#include "WAVM/Runtime/Intrinsics.h"
#include "WAVM/Runtime/Runtime.h"
#include "process.h"

using namespace WAVM::IR;
using namespace WAVM::Runtime;
using namespace Wavix;

namespace Wavix {
	std::string sysroot;
	bool isTracingSyscalls = false;

	WAVM_DEFINE_INTRINSIC_MODULE(wavix);
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wavix,
						  "__invalid_syscall",
						  I32,
						  __invalid_syscall,
						  I32 n,
						  I32 a,
						  I32 b,
						  I32 c,
						  I32 d,
						  I32 e,
						  I32 f)
{
	traceSyscallf("__invalid_syscall", "(%i, %i, %i, %i, %i, %i, %i)", n, a, b, c, d, e, f);
	throwException(ExceptionTypes::calledUnimplementedIntrinsic);
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wavix, "__syscall_membarrier", I32, __syscall_membarrier, I32 dummy)
{
	return 0;
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wavix, "setjmp", I32, wavix_setjmp, U32 bufferAddress)
{
	traceSyscallf("setjmp", "(0x%08x)", bufferAddress);
	return 0;
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wavix, "longjmp", void, wavix_longjmp, U32 bufferAddress, I32 value)
{
	traceSyscallf("longjmp", "(0x%08x, %i)", bufferAddress, value);
	throwException(ExceptionTypes::calledUnimplementedIntrinsic);
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wavix,
						  "__syscall_futex",
						  I32,
						  __syscall_futex,
						  I32 a,
						  I32 b,
						  I32 c,
						  I32 d,
						  I32 e,
						  I32 f)
{
	traceSyscallf("futex", "(%i, %i, %i, %i, %i, %i)", a, b, c, d, e, f);
	throwException(ExceptionTypes::calledUnimplementedIntrinsic);
}

// Command-line arguments

WAVM_DEFINE_INTRINSIC_FUNCTION(wavix, "__wavix_get_num_args", U32, __wavix_get_num_args)
{
	return coerce32bitAddress(currentProcess->args.size());
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wavix,
						  "__wavix_get_arg_length",
						  U32,
						  __wavix_get_arg_length,
						  U32 argIndex)
{
	if(argIndex < currentProcess->args.size())
	{
		const Uptr safeArgIndex
			= Platform::branchlessMin((Uptr)argIndex, currentProcess->args.size() - 1);
		return coerce32bitAddress(currentProcess->args[safeArgIndex].size() + 1);
	}
	else
	{
		throwException(ExceptionTypes::invalidArgument);
	}
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wavix,
						  "__wavix_get_arg",
						  void,
						  __wavix_get_arg,
						  U32 argIndex,
						  U32 bufferAddress,
						  U32 numCharsInBuffer)
{
	Memory* memory = currentThread->process->memory;
	if(argIndex < currentProcess->args.size())
	{
		const Uptr safeArgIndex
			= Platform::branchlessMin((Uptr)argIndex, currentProcess->args.size() - 1);
		const Uptr numChars = currentProcess->args[safeArgIndex].size();
		if(numChars + 1 <= Uptr(numCharsInBuffer))
		{
			memcpy(memoryArrayPtr<char>(memory, bufferAddress, numCharsInBuffer),
				   currentProcess->args[safeArgIndex].c_str(),
				   numChars);
			memoryRef<char>(memory, bufferAddress + numChars) = 0;
		}
		else
		{
			throwException(ExceptionTypes::invalidArgument);
		}
	}
	else
	{
		throwException(ExceptionTypes::invalidArgument);
	}
}

// Resource limits/usage

WAVM_DEFINE_INTRINSIC_FUNCTION(wavix, "__syscall_setrlimit", I32, __syscall_setrlimit, I32 a, I32 b)
{
	traceSyscallf("setrlimit", "(%i,%i)", a, b);
	throwException(ExceptionTypes::calledUnimplementedIntrinsic);
}
WAVM_DEFINE_INTRINSIC_FUNCTION(wavix, "__syscall_getrlimit", I32, __syscall_getrlimit, I32 a, I32 b)
{
	traceSyscallf("getrlimit", "(%i,%i)", a, b);
	throwException(ExceptionTypes::calledUnimplementedIntrinsic);
}
WAVM_DEFINE_INTRINSIC_FUNCTION(wavix, "__syscall_ugetrlimit", I32, __syscall_ugetrlimit, I32 a, I32 b)
{
	traceSyscallf("ugetrlimit", "(%i,%i)", a, b);
	throwException(ExceptionTypes::calledUnimplementedIntrinsic);
}
WAVM_DEFINE_INTRINSIC_FUNCTION(wavix,
						  "__syscall_prlimit64",
						  I32,
						  __syscall_prlimit64,
						  I32 a,
						  I32 b,
						  I32 c,
						  I32 d)
{
	traceSyscallf("prlimit64", "(%i,%i,%i,%i)", a, b, c, d);
	throwException(ExceptionTypes::calledUnimplementedIntrinsic);
}
WAVM_DEFINE_INTRINSIC_FUNCTION(wavix, "__syscall_getrusage", I32, __syscall_getrusage, I32 a, I32 b)
{
	traceSyscallf("getrusage", "(%i,%i)", a, b);
	throwException(ExceptionTypes::calledUnimplementedIntrinsic);
}

// Sockets

WAVM_DEFINE_INTRINSIC_FUNCTION(wavix, "__syscall_socketcall", I32, __syscall_socketcall, I32 a, I32 b)
{
	traceSyscallf("socketcall", "(%i,%i)", a, b);
	return -1;
	// throwException(ExceptionTypes::calledUnimplementedIntrinsic);
}

// System information

struct wavix_utsname
{
	char sysName[65];
	char nodeName[65];
	char release[65];
	char version[65];
	char machine[65];
	char domainName[65];
};

WAVM_DEFINE_INTRINSIC_FUNCTION(wavix, "__syscall_uname", I32, __syscall_uname, U32 resultAddress)
{
	Memory* memory = currentThread->process->memory;
	traceSyscallf("uname", "(0x%08x)", resultAddress);
	wavix_utsname& result = memoryRef<wavix_utsname>(memory, resultAddress);
	WAVM_SCOPED_DISABLE_SECURE_CRT_WARNINGS(strcpy(result.sysName, "Wavix"));
	WAVM_SCOPED_DISABLE_SECURE_CRT_WARNINGS(strcpy(result.nodeName, "utsname::nodename"));
	WAVM_SCOPED_DISABLE_SECURE_CRT_WARNINGS(strcpy(result.release, "utsname::release"));
	WAVM_SCOPED_DISABLE_SECURE_CRT_WARNINGS(strcpy(result.version, "utsname::version"));
	WAVM_SCOPED_DISABLE_SECURE_CRT_WARNINGS(strcpy(result.machine, "wasm32"));
	WAVM_SCOPED_DISABLE_SECURE_CRT_WARNINGS(strcpy(result.domainName, "utsname::domainname"));
	return 0;
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wavix, "__syscall_sysinfo", I32, __syscall_sysinfo, I32 a)
{
	traceSyscallf("sysinfo", "(%i)", a);
	throwException(ExceptionTypes::calledUnimplementedIntrinsic);
}

// Signals

WAVM_DEFINE_INTRINSIC_FUNCTION(wavix,
						  "__syscall_rt_sigaction",
						  I32,
						  __syscall_rt_sigaction,
						  I32 a,
						  I32 b,
						  I32 c)
{
	traceSyscallf("rt_sigaction", "(%u,%u,%u)", a, b, c);
	// throwException(ExceptionTypes::calledUnimplementedIntrinsic);
	return 0;
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wavix, "__syscall_tgkill", I32, __syscall_tgkill, I32 a, I32 b, I32 c)
{
	throwException(ExceptionTypes::calledUnimplementedIntrinsic);
}

// Time

enum class ClockId : I32
{
	realtime = 0,
	monotonic = 1,
};

WAVM_DEFINE_INTRINSIC_FUNCTION(wavix,
						  "__syscall_clock_gettime",
						  I32,
						  __syscall_clock_gettime,
						  U32 clockId,
						  U32 resultAddress)
{
	traceSyscallf("clock_gettime", "(%u,0x%08x)", clockId, resultAddress);

	Memory* memory = currentThread->process->memory;
	wavix_timespec& result = memoryRef<wavix_timespec>(memory, resultAddress);

	static std::atomic<U64> hackedClock;
	const U64 currentClock = hackedClock;

	switch((ClockId)clockId)
	{
	case ClockId::realtime:
	case ClockId::monotonic:
		result.tv_sec = I32(currentClock / 1000000000);
		result.tv_nsec = I32(currentClock % 1000000000);
		++hackedClock;
		break;
	default: throwException(ExceptionTypes::calledUnimplementedIntrinsic);
	};

	return 0;
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wavix,
						  "__syscall_gettimeofday",
						  I32,
						  __syscall_gettimeofday,
						  I32 a,
						  I32 b)
{
	traceSyscallf("gettimeofday", "(%i,%i)", a, b);
	throwException(ExceptionTypes::calledUnimplementedIntrinsic);
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wavix,
						  "__syscall_setitimer",
						  I32,
						  __syscall_setitimer,
						  I32 a,
						  I32 b,
						  I32 c)
{
	traceSyscallf("setitimer", "(%i,%i,%i)", a, b, c);
	throwException(ExceptionTypes::calledUnimplementedIntrinsic);
}

// Users/groups

WAVM_DEFINE_INTRINSIC_FUNCTION(wavix, "__syscall_getuid32", I32, __syscall_getuid32, I32 dummy)
{
	traceSyscallf("getuid32", "");
	return 1;
}
WAVM_DEFINE_INTRINSIC_FUNCTION(wavix, "__syscall_getgid32", I32, __syscall_getgid32, I32 dummy)
{
	traceSyscallf("getgid32", "");
	return 1;
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wavix, "__syscall_geteuid32", I32, __syscall_geteuid32, I32 dummy)
{
	traceSyscallf("geteuid32", "");
	return 1;
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wavix, "__syscall_getegid32", I32, __syscall_getegid32, I32 dummy)
{
	traceSyscallf("geteuid32", "");
	return 1;
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wavix, "__syscall_setreuid32", I32, __syscall_setreuid32, I32 a, I32 b)
{
	throwException(ExceptionTypes::calledUnimplementedIntrinsic);
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wavix, "__syscall_setregid32", I32, __syscall_setregid32, I32 a, I32 b)
{
	throwException(ExceptionTypes::calledUnimplementedIntrinsic);
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wavix, "__syscall_getgroups32", I32, __syscall_getgroups32, I32 a, I32 b)
{
	throwException(ExceptionTypes::calledUnimplementedIntrinsic);
}

// Exceptions

WAVM_DEFINE_INTRINSIC_FUNCTION(wavix, "_Unwind_RaiseException", I32, Wavix_Unwind_RaiseException, I32 a)
{
	throwException(ExceptionTypes::calledUnimplementedIntrinsic);
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wavix,
						  "_Unwind_DeleteException",
						  void,
						  Wavix_Unwind_DeleteException,
						  I32 a)
{
	throwException(ExceptionTypes::calledUnimplementedIntrinsic);
}

void showHelp()
{
	Log::printf(Log::error,
				"Usage: wavix [options] <executable path> [--] [arguments]\n"
				"  Runs a Wavix executable.\n"
				"  <executable path> Host path to the Wavix executable\n"
				"  --trace-syscalls  Trace Wavix syscalls to stdout\n"
				"  --sysroot <path>  Sets the system root directory to the given path.\n"
				"                      Defaults to the CWD. All Wavix file paths will be relative\n"
				"                      to sysroot, with the exception of <executable path>\n"
				"  --                Pass all subsequent arguments to the Wavix executable\n");
	//          "--------------------------------------------------------------------------------"
	//          <- 80 chars wide
}

int main(int argc, const char** argv)
{
	Wavix::sysroot = Platform::getCurrentWorkingDirectory();

	const char* filename = nullptr;
	while(*++argv)
	{
		if(!strcmp(*argv, "--sysroot"))
		{
			if(*++argv == nullptr)
			{
				Log::printf(Log::error,
							"Expected path following '--sysroot', but it was the last argument.\n");
				return EXIT_FAILURE;
			}
			Wavix::sysroot = *argv;
		}
		else if(!strcmp(*argv, "--trace-syscalls"))
		{
			Log::setCategoryEnabled(Log::debug, true);
			Wavix::isTracingSyscalls = true;
		}
		else if(!strcmp(*argv, "--"))
		{
			++argv;
			break;
		}
		else if(!strcmp(*argv, "--help") || !strcmp(*argv, "-h"))
		{
			showHelp();
			return EXIT_SUCCESS;
		}
		else if(!filename)
		{
			filename = *argv;
		}
		else
		{
			break;
		}
	}

	std::vector<std::string> processArgs;
	while(*argv) { processArgs.push_back(*argv++); };

	if(!filename)
	{
		showHelp();
		return EXIT_FAILURE;
	}

	// Create a dummy root process+thread.
	Wavix::Process* initProcess = new Wavix::Process;
	Wavix::Thread* initThread = new Wavix::Thread(initProcess, nullptr, nullptr, nullptr);

	// Spawn a process to execute the specified binary.
	Wavix::Process* process = Wavix::spawnProcess(initProcess, filename, processArgs, {}, "/");
	if(!process)
	{
		Log::printf(Log::error, "Failed to spawn \"%s\".\n", filename);
		return EXIT_FAILURE;
	}

	// Wait for the process to exit.
	{
		Lock<Platform::Mutex> waiterLock(process->waitersMutex);
		process->waiters.push_back(initThread);
	}

	while(!initThread->wakeEvent.wait(UINT64_MAX)) {};

	{
		Lock<Platform::Mutex> waiterLock(process->waitersMutex);
		auto waiterIt = std::find(process->waiters.begin(), process->waiters.end(), initThread);
		if(waiterIt != process->waiters.end()) { process->waiters.erase(waiterIt); }
	}

	Lock<Platform::Mutex> threadsLock(process->threadsMutex);
	return int(process->threads[0]->result);
}
