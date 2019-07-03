#include "WAVM/WASI/WASI.h"
#include "./WASIPrivate.h"
#include "./WASITypes.h"
#include "WAVM/IR/Types.h"
#include "WAVM/Inline/BasicTypes.h"
#include "WAVM/Inline/IndexMap.h"
#include "WAVM/Logging/Logging.h"
#include "WAVM/Platform/Clock.h"
#include "WAVM/Platform/Defines.h"
#include "WAVM/Platform/Diagnostics.h"
#include "WAVM/Platform/File.h"
#include "WAVM/Platform/Intrinsic.h"
#include "WAVM/Platform/Random.h"
#include "WAVM/Platform/Thread.h"
#include "WAVM/Runtime/Intrinsics.h"
#include "WAVM/Runtime/Runtime.h"
#include "WAVM/VFS/VFS.h"

using namespace WAVM;
using namespace WAVM::IR;
using namespace WAVM::Runtime;
using namespace WAVM::WASI;

namespace WAVM { namespace WASI {
	WAVM_DEFINE_INTRINSIC_MODULE(wasi);
}}

struct ExitException
{
	__wasi_exitcode_t exitCode;
};

bool ProcessResolver::resolve(const std::string& moduleName,
							  const std::string& exportName,
							  ExternType type,
							  Object*& outObject)
{
	const auto& namedInstance = moduleNameToInstanceMap.get(moduleName);
	if(namedInstance)
	{
		outObject = getInstanceExport(*namedInstance, exportName);
		if(outObject)
		{
			if(isA(outObject, type)) { return true; }
			else
			{
				Log::printf(Log::debug,
							"Resolved import %s.%s to a %s, but was expecting %s\n",
							moduleName.c_str(),
							exportName.c_str(),
							asString(getExternType(outObject)).c_str(),
							asString(type).c_str());
				return false;
			}
		}
	}

	return false;
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wasi,
							   "poll_oneoff",
							   __wasi_errno_return_t,
							   wasi_poll_oneoff,
							   WASIAddress inAddress,
							   WASIAddress outAddress,
							   WASIAddress numSubscriptions,
							   WASIAddress outNumEventsAddress)
{
	UNIMPLEMENTED_SYSCALL("poll_oneoff",
						  "(" WASIADDRESS_FORMAT ", " WASIADDRESS_FORMAT ", %u, " WASIADDRESS_FORMAT
						  ")",
						  inAddress,
						  outAddress,
						  numSubscriptions,
						  outNumEventsAddress);
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wasi, "proc_exit", void, wasi_proc_exit, __wasi_exitcode_t exitCode)
{
	TRACE_SYSCALL("proc_exit", "(%u)", exitCode);
	throw ExitException{exitCode};
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wasi,
							   "proc_raise",
							   __wasi_errno_return_t,
							   wasi_proc_raise,
							   __wasi_signal_t sig)
{
	// proc_raise will possibly be removed: https://github.com/WebAssembly/WASI/issues/7
	UNIMPLEMENTED_SYSCALL("proc_raise", "(%u)", sig);
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wasi,
							   "random_get",
							   __wasi_errno_return_t,
							   wasi_random_get,
							   WASIAddress bufferAddress,
							   WASIAddress numBufferBytes)
{
	TRACE_SYSCALL("random_get", "(" WASIADDRESS_FORMAT ", %u)", bufferAddress, numBufferBytes);

	Process* process = getProcessFromContextRuntimeData(contextRuntimeData);

	U8* buffer = memoryArrayPtr<U8>(process->memory, bufferAddress, numBufferBytes);
	Platform::getCryptographicRNG(buffer, numBufferBytes);

	return TRACE_SYSCALL_RETURN(__WASI_ESUCCESS);
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wasi,
							   "sock_recv",
							   __wasi_errno_return_t,
							   wasi_sock_recv,
							   __wasi_fd_t sock,
							   WASIAddress ri_data,
							   WASIAddress ri_data_len,
							   __wasi_riflags_t ri_flags,
							   WASIAddress ro_datalen,
							   WASIAddress ro_flags)
{
	UNIMPLEMENTED_SYSCALL("sock_recv",
						  "(%u, " WASIADDRESS_FORMAT ", %u, 0x%04x, " WASIADDRESS_FORMAT
						  ", " WASIADDRESS_FORMAT ")",
						  sock,
						  ri_data,
						  ri_data_len,
						  ri_flags,
						  ro_datalen,
						  ro_flags);
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wasi,
							   "sock_send",
							   __wasi_errno_return_t,
							   wasi_sock_send,
							   __wasi_fd_t sock,
							   WASIAddress si_data,
							   WASIAddress si_data_len,
							   __wasi_siflags_t si_flags,
							   WASIAddress so_datalen)
{
	UNIMPLEMENTED_SYSCALL("sock_send",
						  "(%u, " WASIADDRESS_FORMAT ", %u, 0x%04x, " WASIADDRESS_FORMAT ")",
						  sock,
						  si_data,
						  si_data_len,
						  si_flags,
						  so_datalen);
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wasi,
							   "sock_shutdown",
							   __wasi_errno_return_t,
							   wasi_sock_shutdown,
							   __wasi_fd_t sock,
							   __wasi_sdflags_t how)
{
	UNIMPLEMENTED_SYSCALL("sock_shutdown", "(%u, 0x%02x)", sock, how);
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wasi, "sched_yield", __wasi_errno_return_t, wasi_sched_yield)
{
	TRACE_SYSCALL("sched_yield", "()");
	Platform::yieldToAnotherThread();
	return TRACE_SYSCALL_RETURN(__WASI_ESUCCESS);
}

WASI::Process::~Process()
{
	for(const WASI::FDE& fd : fds)
	{
		if(fd.close() != VFS::Result::success)
		{
			Log::printf(Log::Category::debug, "Error while closing file because of process exit\n");
		}
	}

	context = nullptr;
	memory = nullptr;
	moduleInstance = nullptr;
	resolver.moduleNameToInstanceMap.clear();
	errorUnless(tryCollectCompartment(std::move(compartment)));
}

WASI::RunResult WASI::run(Runtime::ModuleConstRefParam module,
						  std::vector<std::string>&& inArgs,
						  std::vector<std::string>&& inEnvs,
						  VFS::FileSystem* fileSystem,
						  VFS::VFD* stdIn,
						  VFS::VFD* stdOut,
						  VFS::VFD* stdErr,
						  I32& outExitCode)
{
	Process* process = new Process;
	process->args = std::move(inArgs);
	process->envs = std::move(inEnvs);
	process->fileSystem = fileSystem;

	process->compartment = createCompartment();
	setUserData(process->compartment, process, nullptr);
	process->context = createContext(process->compartment);

	process->resolver.moduleNameToInstanceMap.set(
		"wasi_unstable",
		Intrinsics::instantiateModule(process->compartment,
									  {WAVM_INTRINSIC_MODULE_REF(wasi),
									   WAVM_INTRINSIC_MODULE_REF(wasiArgsEnvs),
									   WAVM_INTRINSIC_MODULE_REF(wasiClocks),
									   WAVM_INTRINSIC_MODULE_REF(wasiFile)},
									  "wasi_unstable"));

	__wasi_rights_t stdioRights = __WASI_RIGHT_FD_READ | __WASI_RIGHT_FD_FDSTAT_SET_FLAGS
								  | __WASI_RIGHT_FD_WRITE | __WASI_RIGHT_FD_FILESTAT_GET
								  | __WASI_RIGHT_POLL_FD_READWRITE;

	process->fds.insertOrFail(0, FDE(stdIn, stdioRights, 0, "/dev/stdin"));
	process->fds.insertOrFail(1, FDE(stdOut, stdioRights, 0, "/dev/stdout"));
	process->fds.insertOrFail(2, FDE(stdErr, stdioRights, 0, "/dev/stderr"));

	if(fileSystem)
	{
		VFS::VFD* rootFD = nullptr;
		const VFS::Result openResult = fileSystem->open(
			"/", VFS::FileAccessMode::none, VFS::FileCreateMode::openExisting, rootFD);
		if(openResult != VFS::Result::success)
		{
			Errors::fatalf("Error opening WASI root directory: %s",
						   VFS::describeResult(openResult));
		}

		process->fds.insertOrFail(3,
								  FDE(rootFD,
									  DIRECTORY_RIGHTS,
									  INHERITING_DIRECTORY_RIGHTS,
									  "/",
									  true,
									  __WASI_PREOPENTYPE_DIR));
	}

	process->processClockOrigin = Platform::getProcessClock();

	const IR::Module& moduleIR = getModuleIR(module);
	LinkResult linkResult = linkModule(moduleIR, process->resolver);

	if(!linkResult.success)
	{
		for(const auto& missingImport : linkResult.missingImports)
		{
			Log::printf(Log::debug,
						"Couldn't resolve import %s.%s : %s\n",
						missingImport.moduleName.c_str(),
						missingImport.exportName.c_str(),
						asString(missingImport.type).c_str());
		}
		delete process;
		return RunResult::linkError;
	}

	process->moduleInstance = instantiateModule(
		process->compartment, module, std::move(linkResult.resolvedImports), "<main module>");

	try
	{
		process->memory = asMemoryNullable(getInstanceExport(process->moduleInstance, "memory"));
		if(!process->memory)
		{
			delete process;
			return RunResult::doesNotExportMemory;
		}

		Function* startFunction = getStartFunction(process->moduleInstance);
		if(startFunction) { invokeFunctionChecked(process->context, startFunction, {}); }

		Function* mainFunction
			= asFunctionNullable(getInstanceExport(process->moduleInstance, "_start"));
		if(!mainFunction)
		{
			delete process;
			return RunResult::noStartFunction;
		}

		if(getFunctionType(mainFunction) != FunctionType())
		{
			Log::printf(Log::Category::debug,
						"WASI module exported _start : %s but expected _start : %s.\n",
						asString(getFunctionType(mainFunction)).c_str(),
						asString(FunctionType()).c_str());
			delete process;
			return RunResult::mistypedStartFunction;
		}

		invokeFunctionChecked(process->context, mainFunction, {});
		outExitCode = 0;
	}
	catch(const ExitException& exitException)
	{
		outExitCode = exitException.exitCode;
	}

	delete process;

	return RunResult::success;
}
