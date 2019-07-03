#include <algorithm>
#include <memory>
#include <new>
#include <utility>

#include "WAVM/IR/Module.h"
#include "WAVM/IR/Types.h"
#include "WAVM/IR/Validate.h"
#include "WAVM/IR/Value.h"
#include "WAVM/Inline/Assert.h"
#include "WAVM/Inline/BasicTypes.h"
#include "WAVM/Inline/ConcurrentHashMap.h"
#include "WAVM/Inline/Errors.h"
#include "WAVM/Inline/Hash.h"
#include "WAVM/Inline/HashMap.h"
#include "WAVM/Inline/I128.h"
#include "WAVM/Inline/IndexMap.h"
#include "WAVM/Inline/Lock.h"
#include "WAVM/Inline/Serialization.h"
#include "WAVM/Logging/Logging.h"
#include "WAVM/Platform/File.h"
#include "WAVM/Platform/Mutex.h"
#include "WAVM/Platform/Thread.h"
#include "WAVM/Runtime/Intrinsics.h"
#include "WAVM/Runtime/Linker.h"
#include "WAVM/Runtime/RuntimeData.h"
#include "WAVM/VFS/VFS.h"
#include "WAVM/WASM/WASM.h"
#include "errno.h"
#include "process.h"
#include "wavix.h"

using namespace WAVM::IR;
using namespace WAVM::Runtime;
using namespace Wavix;

namespace Wavix {
	thread_local Thread* currentThread = nullptr;
	thread_local Process* currentProcess = nullptr;

	WAVM_DEFINE_INTRINSIC_MODULE(wavixProcess);
}

static ConcurrentHashMap<I32, Process*> pidToProcessMap;

static Platform::Mutex processesMutex;
static IndexMap<I32, Process*> processes(1, INT32_MAX);

struct RootResolver : Resolver
{
	HashMap<std::string, ModuleInstance*> moduleNameToInstanceMap;

	bool resolve(const std::string& moduleName,
				 const std::string& exportName,
				 ExternType type,
				 Object*& outObject) override
	{
		auto namedInstance = moduleNameToInstanceMap.get(moduleName);
		if(namedInstance)
		{
			outObject = getInstanceExport(*namedInstance, exportName);
			return outObject && isA(outObject, type);
		}
		return false;
	}
};

struct ExitThreadException
{
	I64 code;
};

WAVM_FORCENOINLINE static void setCurrentThreadAndProcess(Thread* newThread)
{
	currentThread = newThread;
	currentProcess = newThread->process;
}

WAVM_FORCENOINLINE static Thread* getCurrentThread() { return currentThread; }

WAVM_FORCENOINLINE static Process* getCurrentProcess() { return currentProcess; }

WAVM_FORCENOINLINE static void signalProcessWaiters(Process* process)
{
	Lock<Platform::Mutex> waitersLock(process->waitersMutex);
	for(Thread* waitingThread : process->waiters) { waitingThread->wakeEvent.signal(); }
}

static I64 mainThreadEntry(void* threadVoid)
{
	setCurrentThreadAndProcess((Thread*)threadVoid);

	catchRuntimeExceptionsOnRelocatableStack(
		[]() {
			I64 result;
			try
			{
				if(getCurrentThread()->startFunction)
				{
					invokeFunctionUnchecked(
						getCurrentThread()->context, getCurrentThread()->startFunction, nullptr);
				}

				result = invokeFunctionUnchecked(
							 getCurrentThread()->context, getCurrentThread()->mainFunction, nullptr)
							 ->i64;
			}
			catch(ExitThreadException const& exitThreadException)
			{
				result = exitThreadException.code;
			}

			Lock<Platform::Mutex> resultLock(getCurrentThread()->resultMutex);
			getCurrentThread()->result = result;
		},
		[](Exception* exception) {
			Log::printf(Log::error, "Runtime exception: %s", describeException(exception).c_str());

			Lock<Platform::Mutex> resultLock(getCurrentThread()->resultMutex);
			getCurrentThread()->result = -1;
			destroyException(exception);
		});

	// Wake any threads waiting for this process to exit.
	signalProcessWaiters(currentProcess);

	return 0;
}

inline bool loadBinaryModuleFromFile(const char* wasmFilename, IR::Module& outModule)
{
	try
	{
		VFS::VFD* vfd = nullptr;
		if(Platform::getHostFS().open(
			   wasmFilename, VFS::FileAccessMode::readOnly, VFS::FileCreateMode::openExisting, vfd)
		   != VFS::Result::success)
		{ return false; }

		U64 numFileBytes = 0;
		errorUnless(vfd->seek(0, VFS::SeekOrigin::end, &numFileBytes) == VFS::Result::success);
		if(numFileBytes > UINTPTR_MAX)
		{
			errorUnless(vfd->close() == VFS::Result::success);
			return false;
		}

		std::unique_ptr<U8[]> fileContents{new U8[numFileBytes]};
		errorUnless(vfd->seek(0, VFS::SeekOrigin::begin) == VFS::Result::success);
		errorUnless(vfd->read(fileContents.get(), numFileBytes) == VFS::Result::success);
		errorUnless(vfd->close() == VFS::Result::success);

		Serialization::MemoryInputStream stream(fileContents.get(), numFileBytes);
		WASM::serialize(stream, outModule);

		return true;
	}
	catch(Serialization::FatalSerializationException const& exception)
	{
		Log::printf(Log::debug,
					"Error deserializing WebAssembly binary file:\n%s\n",
					exception.message.c_str());
		return false;
	}
	catch(IR::ValidationException const& exception)
	{
		Log::printf(Log::debug,
					"Error validating WebAssembly binary file:\n%s\n",
					exception.message.c_str());
		return false;
	}
	catch(std::bad_alloc const&)
	{
		Log::printf(
			Log::debug,
			"Failed to allocate memory during WASM module load: input is likely malformed.\n");
		return false;
	}
}

ModuleInstance* loadModule(Process* process, const char* hostFilename)
{
	// Load the module.
	IR::Module module;
	if(!loadBinaryModuleFromFile(hostFilename, module)) { return nullptr; }

	// Link the module with the Wavix intrinsics.
	RootResolver rootResolver;
	ModuleInstance* wavixIntrinsicModuleInstance
		= Intrinsics::instantiateModule(process->compartment,
										{WAVM_INTRINSIC_MODULE_REF(wavix),
										 WAVM_INTRINSIC_MODULE_REF(wavixFile),
										 WAVM_INTRINSIC_MODULE_REF(wavixProcess),
										 WAVM_INTRINSIC_MODULE_REF(wavixMemory)},
										"WavixIntrinsics");
	rootResolver.moduleNameToInstanceMap.set("env", wavixIntrinsicModuleInstance);

	LinkResult linkResult = linkModule(module, rootResolver);
	if(!linkResult.success)
	{
		Log::printf(Log::debug, "Failed to link module:\n");
		for(auto& missingImport : linkResult.missingImports)
		{
			Log::printf(Log::debug,
						"Missing import: module=\"%s\" export=\"%s\" type=\"%s\"\n",
						missingImport.moduleName.c_str(),
						missingImport.exportName.c_str(),
						asString(missingImport.type).c_str());
		}
		return nullptr;
	}

	// Instantiate the module.
	return instantiateModule(process->compartment,
							 compileModule(module),
							 std::move(linkResult.resolvedImports),
							 hostFilename);
}

Thread* executeModule(Process* process, ModuleInstance* moduleInstance)
{
	// Look up the module's start, and main functions.
	Function* startFunction = getStartFunction(moduleInstance);
	Function* mainFunction = asFunctionNullable(getInstanceExport(moduleInstance, "_start"));

	// Validate that the module exported a main function, and that it is the expected type.
	if(!mainFunction)
	{
		Log::printf(Log::debug, "Module does not export _start function");
		return nullptr;
	}

	FunctionType mainFunctionType = getFunctionType(mainFunction);
	if(mainFunctionType != FunctionType())
	{
		Log::printf(Log::debug,
					"Module _start signature is %s, but ()->() was expected.\n",
					asString(mainFunctionType).c_str());
		return nullptr;
	}

	// Create the context and Wavix Thread object for the main thread.
	Context* mainContext = Runtime::createContext(process->compartment);
	Thread* mainThread = new Thread(process, mainContext, startFunction, mainFunction);

	// Start the process's main thread.
	enum
	{
		mainThreadNumStackBytes = 1 * 1024 * 1024
	};
	Platform::createThread(mainThreadNumStackBytes, &mainThreadEntry, mainThread);

	return mainThread;
}

Process* Wavix::spawnProcess(Process* parent,
							 const char* hostFilename,
							 const std::vector<std::string>& args,
							 const std::vector<std::string>& envs,
							 const std::string& cwd)
{
	// Create the process and compartment.
	Process* process = new Process;
	process->compartment = Runtime::createCompartment();
	process->envs = envs;
	process->args = args;
	process->args.insert(process->args.begin(), hostFilename);

	process->cwd = cwd;

	process->parent = parent;
	if(parent)
	{
		Lock<Platform::Mutex> childrenLock(parent->childrenMutex);
		parent->children.push_back(process);
	}

	// Initialize the process's standard IO file descriptors.
	process->files.insertOrFail(0, Platform::getStdFD(Platform::StdDevice::in));
	process->files.insertOrFail(1, Platform::getStdFD(Platform::StdDevice::out));
	process->files.insertOrFail(2, Platform::getStdFD(Platform::StdDevice::err));

	// Allocate a PID for the process.
	{
		Lock<Platform::Mutex> processesLock(processesMutex);
		process->id = processes.add(-1, process);
		if(process->id == -1) { return nullptr; }
	}

	// Add the process to the PID->process hash table.
	pidToProcessMap.addOrFail(process->id, process);

	// Load the module.
	ModuleInstance* moduleInstance = loadModule(process, hostFilename);
	if(!moduleInstance) { return nullptr; }

	// Get the module's memory and table.
	process->memory = asMemoryNullable(getInstanceExport(moduleInstance, "memory"));
	process->table
		= asTableNullable(getInstanceExport(moduleInstance, "__indirect_function_table"));

	if(!process->memory || !process->table) { return nullptr; }

	Thread* mainThread = executeModule(process, moduleInstance);
	{
		Lock<Platform::Mutex> threadsLock(process->threadsMutex);
		process->threads.push_back(mainThread);
	}

	return process;
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wavixProcess, "__syscall_exit", I32, __syscall_exit, I32 exitCode)
{
	traceSyscallf("exit", "(%i)", exitCode);

	throw ExitThreadException{exitCode};
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wavixProcess,
						  "__syscall_exit_group",
						  I32,
						  __syscall_exit_group,
						  I32 exitCode)
{
	traceSyscallf("exit_group", "(%i)", exitCode);

	throw ExitThreadException{exitCode};
}

WAVM_DEFINE_INTRINSIC_FUNCTION_WITH_CONTEXT_SWITCH(wavix,
											  "__syscall_fork",
											  I32,
											  __syscall_fork,
											  I32 dummy)
{
	Process* originalProcess = currentProcess;
	wavmAssert(originalProcess);

	traceSyscallf("fork", "");

	// Create a new process with a clone of the original's runtime compartment.
	auto newProcess = new Process;
	newProcess->compartment = cloneCompartment(originalProcess->compartment);
	newProcess->args = originalProcess->args;
	newProcess->envs = originalProcess->envs;

	// Look up the new process's memory and table objects by finding the objects with the same IDs
	// as the original process's memory and table objects in the cloned compartment.
	wavmAssert(originalProcess->memory);
	wavmAssert(originalProcess->table);
	newProcess->memory = remapToClonedCompartment(originalProcess->memory, newProcess->compartment);
	newProcess->table = remapToClonedCompartment(originalProcess->table, newProcess->compartment);
	wavmAssert(newProcess->memory);
	wavmAssert(newProcess->table);

	newProcess->parent = originalProcess;
	{
		Lock<Platform::Mutex> childrenLock(originalProcess->childrenMutex);
		originalProcess->children.push_back(newProcess);
	}

	// Copy the original process's working directory and open files to the new process.
	{
		Lock<Platform::Mutex> cwdLock(originalProcess->cwdMutex);
		newProcess->cwd = originalProcess->cwd;
	}
	{
		Lock<Platform::Mutex> filesLock(originalProcess->filesMutex);
		newProcess->files = originalProcess->files;
	}

	// Allocate a PID for the new process.
	{
		Lock<Platform::Mutex> processesLock(processesMutex);
		newProcess->id = processes.add(-1, newProcess);
		if(newProcess->id == -1)
		{
			return Intrinsics::resultInContextRuntimeData<I32>(contextRuntimeData, -ErrNo::eagain);
		}
	}

	// Add the process to the PID->process hash table.
	pidToProcessMap.addOrFail(newProcess->id, newProcess);

	// Create a new Wavix Thread with a clone of the original's runtime context.
	auto newContext
		= cloneContext(getContextFromRuntimeData(contextRuntimeData), newProcess->compartment);
	Thread* newThread = new Thread(newProcess,
								   newContext,
								   getCurrentThread()->startFunction,
								   getCurrentThread()->mainFunction);
	newProcess->threads.push_back(newThread);

	// Fork the current platform thread.
	Platform::Thread* platformThread = Platform::forkCurrentThread();
	if(platformThread)
	{ return Intrinsics::resultInContextRuntimeData<I32>(contextRuntimeData, 1); }
	else
	{
		// Move the newProcess pointer into the thread-local currentProcess variable. Since some
		// compilers will cache a pointer to thread-local data that's accessed multiple times in one
		// function, and currentProcess is accessed before calling forkCurrentThread, we can't
		// directly write to it in this function in case the compiler tries to write to the original
		// thread's currentProcess variable. Instead, call a WAVM_FORCENOINLINE function
		// (setCurrentProcess) to set the variable.
		setCurrentThreadAndProcess(newThread);

		// Switch the contextRuntimeData to point to the new context's runtime data.
		contextRuntimeData = getContextRuntimeData(newContext);

		return Intrinsics::resultInContextRuntimeData<I32>(contextRuntimeData, 0);
	}
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wavixProcess,
						  "__syscall_execve",
						  I32,
						  __syscall_execve,
						  U32 pathAddress,
						  U32 argsAddress,
						  U32 envsAddress)
{
	Memory* memory = currentThread->process->memory;

	std::string pathString = readUserString(memory, pathAddress);
	std::vector<std::string> args;
	std::vector<std::string> envs;

	Runtime::unwindSignalsAsExceptions([&] {
		while(true)
		{
			const U32 argStringAddress = memoryRef<U32>(memory, argsAddress);
			if(!argStringAddress) { break; }
			args.push_back(readUserString(memory, argStringAddress));
			argsAddress += sizeof(U32);
		};
		while(true)
		{
			const U32 envStringAddress = memoryRef<U32>(memory, envsAddress);
			if(!envStringAddress) { break; }
			envs.push_back(readUserString(memory, envStringAddress));
			envsAddress += sizeof(U32);
		};
	});

	if(isTracingSyscalls)
	{
		std::string argsString;
		for(const auto& argString : args)
		{
			if(argsString.size()) { argsString += ", "; }
			argsString += '\"';
			argsString += argString;
			argsString += '\"';
		}
		std::string envsString;
		for(const auto& envString : args)
		{
			if(envsString.size()) { envsString += ", "; }
			envsString += '\"';
			envsString += envString;
			envsString += '\"';
		}
		traceSyscallf("execve",
					  "(\"%s\", {%s}, {%s})",
					  pathString.c_str(),
					  argsString.c_str(),
					  envsString.c_str());
	}

	// Update the process args/envs.
	{
		Lock<Platform::Mutex> argsEnvLock(currentProcess->argsEnvMutex);
		currentProcess->args = args;
		currentProcess->envs = envs;
	}

	// Load the module.
	ModuleInstance* moduleInstance
		= loadModule(currentProcess, (sysroot + "/" + pathString).c_str());
	if(!moduleInstance) { return -ErrNo::enoent; }

	// Execute the module in a new thread.
	Thread* mainThread = executeModule(currentProcess, moduleInstance);
	{
		Lock<Platform::Mutex> threadsLock(currentProcess->threadsMutex);
		currentProcess->threads.clear();
		currentProcess->threads.push_back(mainThread);
	}

	// Exit the calling thread.
	throw ExitThreadException{-1};

	WAVM_UNREACHABLE();
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wavixProcess, "__syscall_kill", I32, __syscall_kill, I32 a, I32 b)
{
	traceSyscallf("kill", "(%i,%i)", a, b);
	throwException(ExceptionTypes::calledUnimplementedIntrinsic);
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wavixProcess, "__syscall_getpid", I32, __syscall_getpid, I32 dummy)
{
	traceSyscallf("getpid", "");
	return currentProcess->id;
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wavixProcess, "__syscall_getppid", I32, __syscall_getppid, I32 dummy)
{
	traceSyscallf("getppid", "");
	if(currentProcess->parent) { return currentProcess->parent->id; }
	else
	{
		return 0;
	}
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wavixProcess,
						  "__syscall_sched_getaffinity",
						  I32,
						  __syscall_sched_getaffinity,
						  I32 a,
						  I32 b,
						  I32 c)
{
	traceSyscallf("sched_getaffinity", "(%i,%i,%i)", a, b, c);
	throwException(ExceptionTypes::calledUnimplementedIntrinsic);
}

#define WAVIX_WNOHANG 1
#define WAVIX_WUNTRACED 2

#define WAVIX_WSTOPPED 2
#define WAVIX_WEXITED 4
#define WAVIX_WCONTINUED 8
#define WAVIX_WNOWAIT 0x1000000

WAVM_DEFINE_INTRINSIC_FUNCTION(wavixProcess,
						  "__syscall_wait4",
						  I32,
						  __syscall_wait4,
						  I32 pid,
						  U32 statusAddress,
						  U32 options,
						  U32 rusageAddress)
{
	Memory* memory = currentThread->process->memory;

	traceSyscallf("wait4", "(%i,0x%08x,%i,0x%08x)", pid, statusAddress, options, rusageAddress);

	if(rusageAddress != 0) { throwException(ExceptionTypes::calledUnimplementedIntrinsic); }

	if(pid < -1)
	{
		// Wait for any child process whose group id == |pid| to change state.
	}
	else if(pid == -1)
	{
		// Wait for any child process to change state.
	}
	else if(pid == 0)
	{
		// Wait for any child process whose group id == this process's group id to change state.
	}
	else if(pid > 0)
	{
		// Wait for the child process whose process id == pid.
	}

	if(options & WAVIX_WNOHANG)
	{
		memoryRef<U32>(memory, statusAddress) = 0;
		return 0;
	}
	else
	{
		std::vector<Process*> waiteeProcesses;
		{
			Lock<Platform::Mutex> childrenLock(currentProcess->childrenMutex);
			waiteeProcesses = currentProcess->children;
		}

		for(Process* child : waiteeProcesses)
		{
			Lock<Platform::Mutex> waiterLock(child->waitersMutex);
			child->waiters.push_back(currentThread);
		}

		while(!currentThread->wakeEvent.wait(WAVM_INT128_MAX)) {};

		for(Process* child : waiteeProcesses)
		{
			Lock<Platform::Mutex> waiterLock(child->waitersMutex);
			auto waiterIt = std::find(child->waiters.begin(), child->waiters.end(), currentThread);
			if(waiterIt != child->waiters.end()) { child->waiters.erase(waiterIt); }
		}

		unwindSignalsAsExceptions([=] { memoryRef<U32>(memory, statusAddress) = WAVIX_WEXITED; });
		return pid == -1 ? 1 : pid;
	}
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wavixProcess, "__syscall_gettid", I32, __syscall_gettid, I32)
{
	traceSyscallf("gettid", "()");
	// throwException(ExceptionTypes::calledUnimplementedIntrinsic);
	return 1;
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wavixProcess,
						  "__syscall_tkill",
						  I32,
						  __syscall_tkill,
						  U32 threadId,
						  I32 signalNumber)
{
	traceSyscallf("tkill", "(%i,%i)", threadId, signalNumber);

	throw ExitThreadException{-1};
}

WAVM_DEFINE_INTRINSIC_FUNCTION(wavixProcess,
						  "__syscall_rt_sigprocmask",
						  I32,
						  __syscall_rt_sigprocmask,
						  I32 how,
						  U32 setAddress,
						  U32 oldSetAddress)
{
	traceSyscallf("rt_sigprocmask", "(%i, 0x%08x, 0x%08x)", how, setAddress, oldSetAddress);
	return 0;
}
