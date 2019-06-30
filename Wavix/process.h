#pragma once

#include <stdint.h>
#include <string>
#include <vector>

#include "WAVM/Inline/BasicTypes.h"
#include "WAVM/Inline/IndexMap.h"
#include "WAVM/Platform/Mutex.h"
#include "WAVM/Runtime/Runtime.h"
#include "WAVM/VFS/VFS.h"

namespace WAVM { namespace Platform {
	struct File;
}}

using namespace WAVM;

namespace Wavix {
	struct Thread;

	struct Process
	{
		Runtime::GCPointer<Runtime::Compartment> compartment;
		Runtime::GCPointer<Runtime::Memory> memory;
		Runtime::GCPointer<Runtime::Table> table;
		Process* parent;
		I32 id;

		Platform::Mutex cwdMutex;
		std::string cwd;

		Platform::Mutex filesMutex;
		IndexMap<I32, VFS::VFD*> files;

		Platform::Mutex childrenMutex;
		std::vector<Process*> children;

		Platform::Mutex argsEnvMutex;
		std::vector<std::string> args;
		std::vector<std::string> envs;

		Platform::Mutex threadsMutex;
		std::vector<Thread*> threads;

		Platform::Mutex waitersMutex;
		std::vector<Thread*> waiters;

		Process() : files(0, INT32_MAX) {}
	};

	extern Process* spawnProcess(Process* parent,
								 const char* hostFilename,
								 const std::vector<std::string>& args,
								 const std::vector<std::string>& envs,
								 const std::string& cwd);
}
