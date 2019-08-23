#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <algorithm>
#include <cstring>
#include <memory>
#include <thread>

#if WAVM_ENABLE_ASAN
#include <sanitizer/asan_interface.h>
#endif

#include "POSIXPrivate.h"
#include "WAVM/Inline/Assert.h"
#include "WAVM/Inline/BasicTypes.h"
#include "WAVM/Inline/Config.h"
#include "WAVM/Inline/Errors.h"
#include "WAVM/Inline/Lock.h"
#include "WAVM/Platform/Intrinsic.h"
#include "WAVM/Platform/Memory.h"
#include "WAVM/Platform/Mutex.h"
#include "WAVM/Platform/Signal.h"
#include "WAVM/Platform/Thread.h"

#ifdef __APPLE__
#define MAP_ANONYMOUS MAP_ANON
#endif

#ifdef __linux__
#define MAP_STACK_FLAGS (MAP_STACK)
#else
#define MAP_STACK_FLAGS 0
#endif

#if WAVM_ENABLE_ASAN
extern "C" void __asan_get_shadow_mapping(Uptr* shadow_scale, Uptr* shadow_offset);
extern "C" Uptr __asan_stack_malloc(Uptr size, Uptr real_stack);
extern "C" void __asan_stack_free(Uptr ptr, Uptr size, Uptr real_stack);
#endif

using namespace WAVM;
using namespace WAVM::Platform;

struct Platform::Thread
{
	pthread_t id;
};

struct CreateThreadArgs
{
	I64 (*entry)(void*);
	void* entryArgument;
};

static constexpr Uptr sigAltStackNumBytes = 65536;

#define ALLOCATE_SIGALTSTACK_ON_MAIN_STACK 1

static bool isAlignedLog2(void* pointer, Uptr alignmentLog2)
{
	return !(reinterpret_cast<Uptr>(pointer) & ((Uptr(1) << alignmentLog2) - 1));
}

void SigAltStack::deinit()
{
	if(base)
	{
		// Disable the sig alt stack.
		// According to the docs, ss_size is ignored if SS_DISABLE is set, but MacOS returns an
		// ENOMEM error if ss_size is too small regardless of whether SS_DISABLE is set.
		stack_t disableAltStack;
		memset(&disableAltStack, 0, sizeof(stack_t));
		disableAltStack.ss_flags = SS_DISABLE;
		disableAltStack.ss_size = sigAltStackNumBytes;
		WAVM_ERROR_UNLESS(!sigaltstack(&disableAltStack, nullptr));

		// Free the alt stack's memory.
		if(!ALLOCATE_SIGALTSTACK_ON_MAIN_STACK)
		{ WAVM_ERROR_UNLESS(!munmap(base, sigAltStackNumBytes)); }
		else
		{
			U8* signalStackMaxAddr = base + sigAltStackNumBytes;
			if(mprotect(signalStackMaxAddr, getBytesPerPage(), PROT_READ | PROT_WRITE) != 0)
			{
				Errors::fatalf("mprotect(0x%" WAVM_PRIxPTR ", %" WAVM_PRIuPTR
							   ", PROT_READ | PROT_WRITE) returned %i.\n",
							   reinterpret_cast<Uptr>(signalStackMaxAddr),
							   getBytesPerPage(),
							   errno);
			}
		}
		base = nullptr;
	}
}

static void getThreadStack(pthread_t thread, U8*& outMinGuardAddr, U8*& outMinAddr, U8*& outMaxAddr)
{
#ifdef __linux__
	// Linux uses pthread_getattr_np/pthread_attr_getstack, and returns a pointer to the minimum
	// address of the stack.
	pthread_attr_t threadAttributes;
	memset(&threadAttributes, 0, sizeof(threadAttributes));
	WAVM_ERROR_UNLESS(!pthread_getattr_np(thread, &threadAttributes));
	Uptr numStackBytes = 0;
	Uptr numGuardBytes = 0;
	WAVM_ERROR_UNLESS(
		!pthread_attr_getstack(&threadAttributes, (void**)&outMinGuardAddr, &numStackBytes));
	WAVM_ERROR_UNLESS(!pthread_attr_getguardsize(&threadAttributes, &numGuardBytes));
	WAVM_ERROR_UNLESS(!pthread_attr_destroy(&threadAttributes));
	outMaxAddr = outMinGuardAddr + numStackBytes;
	outMinAddr = outMinGuardAddr + numGuardBytes;
#elif defined(__APPLE__)
	// MacOS uses pthread_get_stackaddr_np, and returns a pointer to the maximum address of the
	// stack.
	outMaxAddr = (U8*)pthread_get_stackaddr_np(thread);
	const Uptr numStackBytes = pthread_get_stacksize_np(thread);
	outMinAddr = outMaxAddr - numStackBytes;
	outMinGuardAddr = outMinAddr - getBytesPerPage();
#elif defined(__WAVIX__)
	Errors::unimplemented("Wavix getThreadStack");
#else
#error unsupported platform
#endif
}

WAVM_NO_ASAN static void touchStackPages(U8* minAddr, Uptr numBytesPerPage)
{
	U8 sum = 0;
	while(true)
	{
		volatile U8* touchAddr = (volatile U8*)alloca(numBytesPerPage / 2);
		sum += *touchAddr;
		if(touchAddr < minAddr + numBytesPerPage) { break; }
	}
}

void SigAltStack::init()
{
	if(!base)
	{
		// Save the original stack information, since mprotecting part of the stack may change the
		// result of pthread_getattr_np on the main thread. This is because glibc parses
		// /proc/self/maps to implement pthread_getattr_np:
		// https://github.com/lattera/glibc/blob/master/nptl/pthread_getattr_np.c#L72
		getThreadStack(pthread_self(), stackMinGuardAddr, stackMinAddr, stackMaxAddr);

		// Allocate a stack to use when handling signals, so stack overflow can be handled safely.
		if(!ALLOCATE_SIGALTSTACK_ON_MAIN_STACK)
		{
			base = (U8*)mmap(nullptr,
							 sigAltStackNumBytes,
							 PROT_READ | PROT_WRITE,
							 MAP_PRIVATE | MAP_ANONYMOUS,
							 -1,
							 0);
		}
		else
		{
			const Uptr pageSizeLog2 = getBytesPerPageLog2();
			const Uptr numBytesPerPage = Uptr(1) << pageSizeLog2;

			// Use the top of the thread's normal stack to handle signals: just protect a page below
			// the pages used by the sigaltstack that will catch stack overflows before they
			// overwrite the sigaltstack.
			// Touch each stack page from bottom to top to ensure that it has been mapped: Linux and
			// possibly other OSes lazily grow the stack mapping as a guard page is hit.
			touchStackPages(stackMinAddr, numBytesPerPage);

			// Protect a page in between the sigaltstack and the rest of the stack.
			U8* signalStackMaxAddr = stackMinAddr + sigAltStackNumBytes;
			WAVM_ERROR_UNLESS(isAlignedLog2(signalStackMaxAddr, pageSizeLog2));
			if(mprotect(signalStackMaxAddr, numBytesPerPage, PROT_NONE) != 0)
			{
				Errors::fatalf("mprotect(0x%" WAVM_PRIxPTR ", %" WAVM_PRIuPTR
							   ", PROT_NONE) returned %i.\n",
							   reinterpret_cast<Uptr>(signalStackMaxAddr),
							   numBytesPerPage,
							   errno);
			}

			base = stackMinAddr;

			// Exclude the sigaltstack region from the saved "non-signal" stack information.
			stackMinGuardAddr = signalStackMaxAddr;
			stackMinAddr = signalStackMaxAddr + numBytesPerPage;
		}

		WAVM_ERROR_UNLESS(base != MAP_FAILED);
		stack_t sigAltStackInfo;
		sigAltStackInfo.ss_size = sigAltStackNumBytes;
		sigAltStackInfo.ss_sp = base;
		sigAltStackInfo.ss_flags = 0;
		WAVM_ERROR_UNLESS(!sigaltstack(&sigAltStackInfo, nullptr));
	}
}

void SigAltStack::getNonSignalStack(U8*& outMinGuardAddr, U8*& outMinAddr, U8*& outMaxAddr)
{
	if(!base) { getThreadStack(pthread_self(), outMinGuardAddr, outMinAddr, outMaxAddr); }
	else
	{
		outMinGuardAddr = stackMinGuardAddr;
		outMinAddr = stackMinAddr;
		outMaxAddr = stackMaxAddr;
	}
}

thread_local SigAltStack Platform::sigAltStack;

WAVM_NO_ASAN static void* createThreadEntry(void* argsVoid)
{
	std::unique_ptr<CreateThreadArgs> args((CreateThreadArgs*)argsVoid);

	sigAltStack.init();

	I64 exitCode = (*args->entry)(args->entryArgument);

	sigAltStack.deinit();

	return reinterpret_cast<void*>(exitCode);
}

Platform::Thread* Platform::createThread(Uptr numStackBytes,
										 I64 (*threadEntry)(void*),
										 void* argument)
{
	auto thread = new Thread;
	auto createArgs = new CreateThreadArgs;
	createArgs->entry = threadEntry;
	createArgs->entryArgument = argument;

	pthread_attr_t threadAttr;
	WAVM_ERROR_UNLESS(!pthread_attr_init(&threadAttr));
	if(numStackBytes != 0)
	{ WAVM_ERROR_UNLESS(!pthread_attr_setstacksize(&threadAttr, numStackBytes)); }

	// Create a new pthread.
	WAVM_ERROR_UNLESS(!pthread_create(&thread->id, &threadAttr, createThreadEntry, createArgs));
	WAVM_ERROR_UNLESS(!pthread_attr_destroy(&threadAttr));

	return thread;
}

void Platform::detachThread(Thread* thread)
{
	WAVM_ERROR_UNLESS(!pthread_detach(thread->id));
	delete thread;
}

I64 Platform::joinThread(Thread* thread)
{
	void* returnValue = nullptr;
	WAVM_ERROR_UNLESS(!pthread_join(thread->id, &returnValue));
	delete thread;
	return reinterpret_cast<I64>(returnValue);
}

Uptr Platform::getNumberOfHardwareThreads() { return std::thread::hardware_concurrency(); }

void Platform::yieldToAnotherThread() { WAVM_ERROR_UNLESS(sched_yield() == 0); }
