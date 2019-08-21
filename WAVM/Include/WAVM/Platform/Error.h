#pragma once

#include <cstdarg>
#include "WAVM/Inline/BasicTypes.h"

namespace WAVM { namespace Platform {
	WAVM_PACKED_STRUCT(struct AssertMetadata {
		const char* condition;
		const char* file;
		U32 line;
	});

	PLATFORM_API void handleAssertionFailure(const AssertMetadata& metadata);
	[[noreturn]] PLATFORM_API void handleFatalError(const char* messageFormat,
													bool printCallStack,
													va_list varArgs);

}}
