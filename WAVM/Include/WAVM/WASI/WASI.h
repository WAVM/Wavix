#pragma once

#include <memory>
#include "WAVM/Inline/BasicTypes.h"
#include "WAVM/Runtime/Runtime.h"

namespace WAVM { namespace VFS {
	struct FileSystem;
	struct VFD;
}}

namespace WAVM { namespace Runtime {
	struct Resolver;
}}

namespace WAVM { namespace WASI {

	struct Process;

	struct ExitException
	{
		U32 exitCode;
	};

	WASI_API std::shared_ptr<Process> createProcess(Runtime::Compartment* compartment,
													std::vector<std::string>&& inArgs,
													std::vector<std::string>&& inEnvs,
													VFS::FileSystem* fileSystem,
													VFS::VFD* stdIn,
													VFS::VFD* stdOut,
													VFS::VFD* stdErr);

	WASI_API Runtime::Resolver* getProcessResolver(const std::shared_ptr<Process>& process);

	WASI_API Runtime::Memory* getProcessMemory(const std::shared_ptr<Process>& process);
	WASI_API void setProcessMemory(const std::shared_ptr<Process>& process,
								   Runtime::Memory* memory);

	enum class SyscallTraceLevel
	{
		none,
		syscalls,
		syscallsWithCallstacks
	};

	WASI_API void setSyscallTraceLevel(SyscallTraceLevel newLevel);
}}
