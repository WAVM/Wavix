#include <errno.h>
#include <pthread.h>
#include <sys/time.h>

#include "WAVM/Inline/Assert.h"
#include "WAVM/Inline/Errors.h"
#include "WAVM/Inline/I128.h"
#include "WAVM/Platform/Clock.h"
#include "WAVM/Platform/Defines.h"
#include "WAVM/Platform/Event.h"

using namespace WAVM;
using namespace WAVM::Platform;

Platform::Event::Event()
{
	static_assert(sizeof(pthreadMutex) == sizeof(pthread_mutex_t), "");
	static_assert(alignof(PthreadMutex) >= alignof(pthread_mutex_t), "");

	static_assert(sizeof(pthreadCond) == sizeof(pthread_cond_t), "");
	static_assert(alignof(PthreadCond) >= alignof(pthread_cond_t), "");

	pthread_condattr_t conditionVariableAttr;
	errorUnless(!pthread_condattr_init(&conditionVariableAttr));

// Set the condition variable to use the monotonic clock for wait timeouts.
#ifndef __APPLE__
	errorUnless(!pthread_condattr_setclock(&conditionVariableAttr, CLOCK_MONOTONIC));
#endif

	errorUnless(!pthread_cond_init((pthread_cond_t*)&pthreadCond, nullptr));
	errorUnless(!pthread_mutex_init((pthread_mutex_t*)&pthreadMutex, nullptr));

	errorUnless(!pthread_condattr_destroy(&conditionVariableAttr));
}

Platform::Event::~Event()
{
	pthread_cond_destroy((pthread_cond_t*)&pthreadCond);
	errorUnless(!pthread_mutex_destroy((pthread_mutex_t*)&pthreadMutex));
}

bool Platform::Event::wait(I128 waitDuration)
{
	errorUnless(!pthread_mutex_lock((pthread_mutex_t*)&pthreadMutex));

	int result;
	if(waitDuration == I128::nan())
	{
		result = pthread_cond_wait((pthread_cond_t*)&pthreadCond, (pthread_mutex_t*)&pthreadMutex);
	}
	else
	{
		// Use the non-POSIX relative time wait on Mac, and an absolute monotonic clock timeout on
		// other POSIX systems.
#ifdef __APPLE__
		timespec waitTimeSpec;
		waitTimeSpec.tv_sec = U64(waitDuration / 1000000000);
		waitTimeSpec.tv_nsec = U64(waitDuration % 1000000000);

		result = pthread_cond_timedwait_relative_np(
			(pthread_cond_t*)&pthreadCond, (pthread_mutex_t*)&pthreadMutex, &waitTimeSpec);
#else
		const I128 untilTime = getMonotonicClock() + waitDuration;
		timespec untilTimeSpec;
		untilTimeSpec.tv_sec = U64(untilTime / 1000000000);
		untilTimeSpec.tv_nsec = U64(untilTime % 1000000000);

		result = pthread_cond_timedwait(
			(pthread_cond_t*)&pthreadCond, (pthread_mutex_t*)&pthreadMutex, &untilTimeSpec);
#endif
	}

	errorUnless(!pthread_mutex_unlock((pthread_mutex_t*)&pthreadMutex));

	if(result == ETIMEDOUT) { return false; }
	else
	{
		errorUnless(!result);
		return true;
	}
}

void Platform::Event::signal() { errorUnless(!pthread_cond_signal((pthread_cond_t*)&pthreadCond)); }
