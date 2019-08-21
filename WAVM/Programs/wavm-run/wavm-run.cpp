#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <utility>
#include <vector>

#include "WAVM/Emscripten/Emscripten.h"
#include "WAVM/IR/FeatureSpec.h"
#include "WAVM/IR/Module.h"
#include "WAVM/IR/Operators.h"
#include "WAVM/IR/Types.h"
#include "WAVM/IR/Validate.h"
#include "WAVM/IR/Value.h"
#include "WAVM/Inline/BasicTypes.h"
#include "WAVM/Inline/CLI.h"
#include "WAVM/Inline/Errors.h"
#include "WAVM/Inline/Hash.h"
#include "WAVM/Inline/HashMap.h"
#include "WAVM/Inline/Serialization.h"
#include "WAVM/Inline/Timing.h"
#include "WAVM/Inline/Version.h"
#include "WAVM/LLVMJIT/LLVMJIT.h"
#include "WAVM/Logging/Logging.h"
#include "WAVM/ObjectCache/ObjectCache.h"
#include "WAVM/Platform/File.h"
#include "WAVM/Platform/Memory.h"
#include "WAVM/Runtime/Linker.h"
#include "WAVM/Runtime/Runtime.h"
#include "WAVM/VFS/SandboxFS.h"
#include "WAVM/WASI/WASI.h"
#include "WAVM/WASM/WASM.h"
#include "WAVM/WASTParse/WASTParse.h"

using namespace WAVM;
using namespace WAVM::IR;
using namespace WAVM::Runtime;

struct RootResolver : Resolver
{
	StubResolver stubResolver;
	HashMap<std::string, ModuleInstance*> moduleNameToInstanceMap;

	RootResolver(Compartment* compartment) : stubResolver(compartment) {}

	bool resolve(const std::string& moduleName,
				 const std::string& exportName,
				 ExternType type,
				 Object*& outObject) override
	{
		auto namedInstance = moduleNameToInstanceMap.get(moduleName);
		if(namedInstance)
		{
			outObject = getInstanceExport(*namedInstance, exportName);
			if(outObject)
			{
				if(isA(outObject, type)) { return true; }
				else
				{
					Log::printf(Log::error,
								"Resolved import %s.%s to a %s, but was expecting %s\n",
								moduleName.c_str(),
								exportName.c_str(),
								asString(getExternType(outObject)).c_str(),
								asString(type).c_str());
				}
			}
		}

		return stubResolver.resolve(moduleName, exportName, type, outObject);
	}
};

static bool loadModule(const char* filename, IR::Module& outModule)
{
	// Read the specified file into an array.
	std::vector<U8> fileBytes;
	if(!loadFile(filename, fileBytes)) { return false; }

	// If the file starts with the WASM binary magic number, load it as a binary irModule.
	static const U8 wasmMagicNumber[4] = {0x00, 0x61, 0x73, 0x6d};
	if(fileBytes.size() >= 4 && !memcmp(fileBytes.data(), wasmMagicNumber, 4))
	{ return WASM::loadBinaryModule(fileBytes.data(), fileBytes.size(), outModule); }
	else
	{
		// Make sure the WAST file is null terminated.
		fileBytes.push_back(0);

		// Load it as a text irModule.
		std::vector<WAST::Error> parseErrors;
		if(!WAST::parseModule(
			   (const char*)fileBytes.data(), fileBytes.size(), outModule, parseErrors))
		{
			Log::printf(Log::error, "Error parsing WebAssembly text file:\n");
			WAST::reportParseErrors(filename, parseErrors);
			return false;
		}

		return true;
	}
}

static bool compileModule(const IR::Module& irModule, ModuleRef& outModule, bool precompiled)
{
	if(!precompiled)
	{
		outModule = Runtime::compileModule(irModule);
		return true;
	}
	else
	{
		const UserSection* precompiledObjectSection = nullptr;
		for(const UserSection& userSection : irModule.userSections)
		{
			if(userSection.name == "wavm.precompiled_object")
			{
				precompiledObjectSection = &userSection;
				break;
			}
		}

		if(!precompiledObjectSection)
		{
			Log::printf(Log::error,
						"Input file did not contain 'wavm.precompiled_object' section.\n");
			return false;
		}
		else
		{
			outModule = Runtime::loadPrecompiledModule(irModule, precompiledObjectSection->data);
			return true;
		}
	}
}

static void reportLinkErrors(const LinkResult& linkResult)
{
	Log::printf(Log::error, "Failed to link module:\n");
	for(auto& missingImport : linkResult.missingImports)
	{
		Log::printf(Log::error,
					"Missing import: module=\"%s\" export=\"%s\" type=\"%s\"\n",
					missingImport.moduleName.c_str(),
					missingImport.exportName.c_str(),
					asString(missingImport.type).c_str());
	}
}

static bool isWASIModule(const IR::Module& irModule)
{
	for(const auto& import : irModule.functions.imports)
	{
		if(import.moduleName == "wasi_unstable") { return true; }
	}

	return false;
}

static bool isEmscriptenModule(const IR::Module& irModule)
{
	if(!irModule.memories.imports.size() || irModule.memories.imports[0].moduleName != "env"
	   || irModule.memories.imports[0].exportName != "memory")
	{ return false; }

	for(const auto& import : irModule.functions.imports)
	{
		if(import.moduleName == "env") { return true; }
	}

	return false;
}

static const char* getSystemListHelpText()
{
	return "  bare        A minimal runtime system.\n"
		   "  emscripten  A system that emulates the Emscripten runtime.\n"
		   "  wasi        A system that implements the WASI ABI.\n";
}

static void showHelp()
{
	Log::printf(Log::error,
				"Usage: wavm-run [options] <program file> [program arguments]\n"
				"  <program file>        The WebAssembly module (.wast/.wasm) to run\n"
				"  [program arguments]   The arguments to pass to the WebAssembly function\n"
				"\n"
				"Options:\n"
				"  -h|--help             Display this message\n"
				"  -v|--version          Display version information\n"
				"  -d|--debug            Write additional debug information to stdout\n"
				"  -f|--function name    Specify function name to run in module (default:main)\n"
				"  --precompiled         Use precompiled object code in program file\n"
				"  --metrics             Write benchmarking information to stdout\n"
				"  --trace               Prints instructions to stdout as they are compiled.\n"
				"  --enable <feature>    Enable the specified feature. See the list of supported\n"
				"                        features below.\n"
				"  --sys=<system>        Specifies the system to host the module. See the list\n"
				"                        of supported sytems below. The default is to detect\n"
				"                        the system based on the module imports/exports.\n"
				"  --mount-root=<dir>    Mounts <dir> as the WASI root directory\n"
				"  --wasi-trace=<level>  Sets the level of WASI tracing:\n"
				"                        - syscalls\n"
				"                        - syscalls-with-callstacks\n"
				"\n"
				"Systems:\n"
				"%s"
				"\n"
				"Features:\n"
				"%s"
				"\n",
				getSystemListHelpText(),
				getFeatureListHelpText());
}

template<Uptr numPrefixChars>
static bool stringStartsWith(const char* string, const char (&prefix)[numPrefixChars])
{
	return !strncmp(string, prefix, numPrefixChars - 1);
}

enum class System
{
	detect,
	bare,
	emscripten,
	wasi
};

struct State
{
	IR::FeatureSpec featureSpec{false};

	// Command-line options.
	const char* filename = nullptr;
	const char* functionName = nullptr;
	const char* rootMountPath = nullptr;
	std::vector<std::string> runArgs;
	System system = System::detect;
	bool precompiled = false;
	WASI::SyscallTraceLevel wasiTraceLavel = WASI::SyscallTraceLevel::none;

	// Objects that need to be cleaned up before exiting.
	GCPointer<Compartment> compartment = createCompartment();
	Emscripten::Instance* emscriptenInstance = nullptr;
	std::shared_ptr<WASI::Process> wasiProcess;
	std::shared_ptr<VFS::FileSystem> sandboxFS;

	~State()
	{
		if(emscriptenInstance) { delete emscriptenInstance; }
		wasiProcess.reset();

		WAVM_ERROR_UNLESS(tryCollectCompartment(std::move(compartment)));
	}

	bool parseCommandLineAndEnvironment(char** argv)
	{
		char** nextArg = argv;
		while(*++nextArg)
		{
			if(!strcmp(*nextArg, "--help") || !strcmp(*nextArg, "-h"))
			{
				showHelp();
				return false;
			}
			else if(!strcmp(*nextArg, "--version") || !strcmp(*nextArg, "-v"))
			{
				Log::printf(Log::output,
							"WAVM version %u.%u.%u\n",
							WAVM_VERSION_MAJOR,
							WAVM_VERSION_MINOR,
							WAVM_VERSION_PATCH);
				return false;
			}
			else if(!strcmp(*nextArg, "--debug") || !strcmp(*nextArg, "-d"))
			{
				Log::setCategoryEnabled(Log::debug, true);
			}
			else if(!strcmp(*nextArg, "--function") || !strcmp(*nextArg, "-f"))
			{
				if(!*++nextArg)
				{
					showHelp();
					return false;
				}
				functionName = *nextArg;
			}
			else if(!strcmp(*nextArg, "--metrics"))
			{
				Log::setCategoryEnabled(Log::metrics, true);
			}
			else if(!strcmp(*nextArg, "--trace"))
			{
				Log::setCategoryEnabled(Log::trace, true);
			}
			else if(stringStartsWith(*nextArg, "--sys="))
			{
				if(system != System::detect)
				{
					Log::printf(Log::error, "'--sys=' may only occur once on the command line.\n");
					return false;
				}

				const char* systemString = *nextArg + strlen("--sys=");
				if(!strcmp(systemString, "bare")) { system = System::bare; }
				else if(!strcmp(systemString, "emscripten"))
				{
					system = System::emscripten;
				}
				else if(!strcmp(systemString, "wasi"))
				{
					system = System::wasi;
				}
				else
				{
					Log::printf(Log::error,
								"Unknown system '%s'. Supported systems:\n"
								"%s"
								"\n",
								systemString,
								getSystemListHelpText());
					return false;
				}
			}
			else if(!strcmp(*nextArg, "--enable"))
			{
				++nextArg;
				if(!*nextArg)
				{
					Log::printf(Log::error, "Expected feature name following '--enable'.\n");
					return false;
				}

				if(!parseAndSetFeature(*nextArg, featureSpec, true))
				{
					Log::printf(Log::error,
								"Unknown feature '%s'. Supported features:\n"
								"%s"
								"\n",
								*nextArg,
								getFeatureListHelpText());
					return false;
				}
			}
			else if(!strcmp(*nextArg, "--precompiled"))
			{
				precompiled = true;
			}
			else if(stringStartsWith(*nextArg, "--mount-root="))
			{
				if(rootMountPath)
				{
					Log::printf(Log::error,
								"--mount-root=' may only occur once on the command line.\n");
					return false;
				}
				rootMountPath = *nextArg + strlen("--mount-root=");
			}
			else if(stringStartsWith(*nextArg, "--wasi-trace="))
			{
				if(wasiTraceLavel != WASI::SyscallTraceLevel::none)
				{
					Log::printf(Log::error,
								"--wasi-trace=' may only occur once on the command line.\n");
					return false;
				}

				const char* levelString = *nextArg + strlen("--mount-root=");

				if(!strcmp(levelString, "syscalls"))
				{ wasiTraceLavel = WASI::SyscallTraceLevel::syscalls; }
				else if(!strcmp(levelString, "syscalls-with-callstacks"))
				{
					wasiTraceLavel = WASI::SyscallTraceLevel::syscallsWithCallstacks;
				}
				else
				{
					Log::printf(Log::error, "Invalid WASI trace level: %s\n", levelString);
					return false;
				}
			}
			else
			{
				filename = *nextArg;
				++nextArg;
				break;
			}
		}

		if(!filename)
		{
			showHelp();
			return false;
		}

		while(*nextArg) { runArgs.push_back(*nextArg++); };

		// Check that the requested features are supported by the host CPU.
		switch(LLVMJIT::validateTarget(LLVMJIT::getHostTargetSpec(), featureSpec))
		{
		case LLVMJIT::TargetValidationResult::valid: break;

		case LLVMJIT::TargetValidationResult::unsupportedArchitecture:
			Log::printf(Log::error, "Host architecture is not supported by WAVM.");
			return false;
		case LLVMJIT::TargetValidationResult::x86CPUDoesNotSupportSSE41:
			Log::printf(Log::error,
						"Host X86 CPU does not support SSE 4.1, which"
						" WAVM requires for WebAssembly SIMD code.\n");
			return false;

		case LLVMJIT::TargetValidationResult::invalidTargetSpec:
		default: WAVM_UNREACHABLE();
		};

		const char* objectCachePath
			= WAVM_SCOPED_DISABLE_SECURE_CRT_WARNINGS(getenv("WAVM_OBJECT_CACHE_DIR"));
		if(objectCachePath)
		{
			Uptr maxBytes = 1024 * 1024 * 1024;

			const char* maxMegabytesEnv
				= WAVM_SCOPED_DISABLE_SECURE_CRT_WARNINGS(getenv("WAVM_OBJECT_CACHE_MAX_MB"));
			if(maxMegabytesEnv)
			{
				int maxMegabytes = atoi(maxMegabytesEnv);
				if(maxMegabytes <= 0)
				{
					Log::printf(
						Log::error,
						"Invalid object cache size \"%s\". Expected an integer greater than 1.",
						maxMegabytesEnv);
					return false;
				}
				maxBytes = Uptr(maxMegabytes) * 1024 * 1024;
			}

			// Calculate a "code key" that identifies the code involved in compiling WebAssembly to
			// object code in the cache. If recompiling the module would produce different object
			// code, the code key should be different, and if recompiling the module would produce
			// the same object code, the code key should be the same.
			LLVMJIT::Version llvmjitVersion = LLVMJIT::getVersion();
			U64 codeKey = 0;
			codeKey = Hash<U64>()(llvmjitVersion.llvmMajor, codeKey);
			codeKey = Hash<U64>()(llvmjitVersion.llvmMinor, codeKey);
			codeKey = Hash<U64>()(llvmjitVersion.llvmPatch, codeKey);
			codeKey = Hash<U64>()(WAVM_VERSION_MAJOR, codeKey);
			codeKey = Hash<U64>()(WAVM_VERSION_MINOR, codeKey);
			codeKey = Hash<U64>()(WAVM_VERSION_PATCH, codeKey);

			// Initialize the object cache.
			std::shared_ptr<Runtime::ObjectCacheInterface> objectCache;
			ObjectCache::OpenResult openResult
				= ObjectCache::open(objectCachePath, maxBytes, codeKey, objectCache);
			switch(openResult)
			{
			case ObjectCache::OpenResult::doesNotExist:
				Log::printf(
					Log::error, "Object cache directory \"%s\" does not exist.\n", objectCachePath);
				return false;
			case ObjectCache::OpenResult::notDirectory:
				Log::printf(Log::error,
							"Object cache path \"%s\" does not refer to a directory.\n",
							objectCachePath);
				return false;
			case ObjectCache::OpenResult::notAccessible:
				Log::printf(
					Log::error, "Object cache path \"%s\" is not accessible.\n", objectCachePath);
				return false;
			case ObjectCache::OpenResult::invalidDatabase:
				Log::printf(
					Log::error, "Object cache database in \"%s\" is not valid.\n", objectCachePath);
				return false;
			case ObjectCache::OpenResult::tooManyReaders:
				Log::printf(Log::error,
							"Object cache database in \"%s\" has too many concurrent readers.\n",
							objectCachePath);
				return false;

			case ObjectCache::OpenResult::success:
				Runtime::setGlobalObjectCache(std::move(objectCache));
				break;
			default: WAVM_UNREACHABLE();
			};
		}

		return true;
	}

	bool initSystem(const IR::Module& irModule)
	{
		// If the user didn't specify a system on the command-line, try to figure it out from the
		// module's imports.
		if(system == System::detect)
		{
			if(isWASIModule(irModule))
			{
				Log::printf(Log::debug, "Module appears to be a WASI module.\n");
				system = System::wasi;
			}
			else if(isEmscriptenModule(irModule))
			{
				Log::printf(Log::debug, "Module appears to be an Emscripten module.\n");
				system = System::emscripten;
			}
			else
			{
				system = System::bare;
			}
		}

		// If a directory to mount as the root filesystem was passed on the command-line, create a
		// SandboxFS for it.
		if(rootMountPath)
		{
			if(system != System::wasi)
			{
				Log::printf(Log::error, "--mount-root may only be used with the WASI system.\n");
				return false;
			}

			std::string absoluteRootMountPath;
			if(rootMountPath[0] == '/' || rootMountPath[0] == '\\' || rootMountPath[0] == '~'
			   || rootMountPath[1] == ':')
			{ absoluteRootMountPath = rootMountPath; }
			else
			{
				absoluteRootMountPath
					= Platform::getCurrentWorkingDirectory() + '/' + rootMountPath;
			}
			sandboxFS = VFS::makeSandboxFS(&Platform::getHostFS(), absoluteRootMountPath);
		}

		if(system == System::emscripten)
		{
			// Instantiate the Emscripten environment.
			emscriptenInstance = Emscripten::instantiate(compartment, irModule);
			if(emscriptenInstance)
			{
				emscriptenInstance->stdIn = Platform::getStdFD(Platform::StdDevice::in);
				emscriptenInstance->stdOut = Platform::getStdFD(Platform::StdDevice::out);
				emscriptenInstance->stdErr = Platform::getStdFD(Platform::StdDevice::err);
			}
		}
		else if(system == System::wasi)
		{
			std::vector<std::string> args = runArgs;
			args.insert(args.begin(), "/proc/1/exe");

			// Create the WASI process.
			wasiProcess = WASI::createProcess(compartment,
											  std::move(args),
											  {},
											  sandboxFS.get(),
											  Platform::getStdFD(Platform::StdDevice::in),
											  Platform::getStdFD(Platform::StdDevice::out),
											  Platform::getStdFD(Platform::StdDevice::err));
		}

		if(wasiTraceLavel != WASI::SyscallTraceLevel::none)
		{
			if(system != System::wasi)
			{
				Log::printf(Log::error, "--wasi-trace may only be used with the WASI system.\n");
				return false;
			}

			WASI::setSyscallTraceLevel(wasiTraceLavel);
		}

		return true;
	}

	int run(char** argv)
	{
		// Parse the command line.
		if(!parseCommandLineAndEnvironment(argv)) { return EXIT_FAILURE; }

		// Load the module.
		IR::Module irModule(featureSpec);
		if(!loadModule(filename, irModule)) { return EXIT_FAILURE; }

		// Compile the module.
		Runtime::ModuleRef module = nullptr;
		if(!compileModule(irModule, module, precompiled)) { return EXIT_FAILURE; }

		// Initialize the system environment.
		if(!initSystem(irModule)) { return EXIT_FAILURE; }

		// Link the module with the intrinsic modules.
		LinkResult linkResult;
		if(system == System::emscripten || system == System::bare)
		{
			RootResolver rootResolver(compartment);

			if(emscriptenInstance)
			{
				rootResolver.moduleNameToInstanceMap.set("env", emscriptenInstance->env);
				rootResolver.moduleNameToInstanceMap.set("asm2wasm", emscriptenInstance->asm2wasm);
				rootResolver.moduleNameToInstanceMap.set("global", emscriptenInstance->global);
			}

			linkResult = linkModule(irModule, rootResolver);
		}
		else if(system == System::wasi)
		{
			Resolver* resolver = WASI::getProcessResolver(wasiProcess);
			linkResult = linkModule(irModule, *resolver);
		}
		else
		{
			WAVM_UNREACHABLE();
		}

		if(!linkResult.success)
		{
			reportLinkErrors(linkResult);
			return EXIT_FAILURE;
		}

		// Instantiate the module.
		ModuleInstance* moduleInstance = instantiateModule(
			compartment, module, std::move(linkResult.resolvedImports), filename);
		if(!moduleInstance) { return EXIT_FAILURE; }

		// Take the module's memory as the WASI process memory.
		if(system == System::wasi)
		{
			Memory* memory = asMemoryNullable(getInstanceExport(moduleInstance, "memory"));
			if(!memory)
			{
				Log::printf(Log::error, "WASM module doesn't export WASI memory.\n");
				return EXIT_FAILURE;
			}
			WASI::setProcessMemory(wasiProcess, memory);
		}

		// Create a WASM execution context.
		Context* context = Runtime::createContext(compartment);

		// Look up the function export to call, validate its type, and set up the invoke arguments.
		Function* function = nullptr;
		std::vector<Value> invokeArgs;
		if(functionName)
		{
			function = asFunctionNullable(getInstanceExport(moduleInstance, functionName));
			if(!function)
			{
				Log::printf(Log::error, "Module does not export '%s'\n", functionName);
				return EXIT_FAILURE;
			}

			const FunctionType functionType = getFunctionType(function);

			if(functionType.params().size() != runArgs.size())
			{
				Log::printf(Log::error,
							"'%s' expects %" WAVM_PRIuPTR
							" argument(s), but command line had %" WAVM_PRIuPTR ".\n",
							functionName,
							Uptr(functionType.params().size()),
							Uptr(runArgs.size()));
				return EXIT_FAILURE;
			}

			for(Uptr argIndex = 0; argIndex < runArgs.size(); ++argIndex)
			{
				const std::string& argString = runArgs[argIndex];
				Value value;
				switch(functionType.params()[argIndex])
				{
				case ValueType::i32: value = (U32)atoi(argString.c_str()); break;
				case ValueType::i64: value = (U64)atol(argString.c_str()); break;
				case ValueType::f32: value = (F32)atof(argString.c_str()); break;
				case ValueType::f64: value = atof(argString.c_str()); break;
				case ValueType::v128:
				case ValueType::anyref:
				case ValueType::funcref:
					Errors::fatalf("Cannot parse command-line argument for %s function parameter",
								   asString(functionType.params()[argIndex]));

				case ValueType::none:
				case ValueType::any:
				case ValueType::nullref:
				default: WAVM_UNREACHABLE();
				}
				invokeArgs.push_back(value);
			}
		}
		else if(system == System::wasi)
		{
			// WASI just calls a _start function with the signature ()->().
			function = asFunctionNullable(getInstanceExport(moduleInstance, "_start"));
			if(!function)
			{
				Log::printf(Log::error, "WASM module doesn't export WASI _start function.\n");
				return EXIT_FAILURE;
			}
			if(getFunctionType(function) != FunctionType())
			{
				Log::printf(Log::error,
							"WASI module exported _start : %s but expected _start : %s.\n",
							asString(getFunctionType(function)).c_str(),
							asString(FunctionType()).c_str());
				return EXIT_FAILURE;
			}
		}
		else
		{
			// Emscripten calls main or _main with a signature (i32, i32)|() -> i32?
			function = asFunctionNullable(getInstanceExport(moduleInstance, "main"));
			if(!function)
			{ function = asFunctionNullable(getInstanceExport(moduleInstance, "_main")); }
			if(!function)
			{
				Log::printf(Log::error, "Module does not export main function\n");
				return EXIT_FAILURE;
			}
			const FunctionType functionType = getFunctionType(function);

			if(functionType.params().size() == 2)
			{
				if(!emscriptenInstance)
				{
					Log::printf(
						Log::error,
						"Module does not declare a default memory object to put arguments in.\n");
					return EXIT_FAILURE;
				}
				else
				{
					std::vector<std::string> args = runArgs;
					args.insert(args.begin(), filename);

					WAVM_ASSERT(emscriptenInstance);
					Emscripten::injectCommandArgs(emscriptenInstance, args, invokeArgs);
				}
			}
			else if(functionType.params().size() > 0)
			{
				Log::printf(Log::error,
							"WebAssembly function requires %" PRIu64
							" argument(s), but only 0 or 2 can be passed!",
							functionType.params().size());
				return EXIT_FAILURE;
			}
		}

		int result = EXIT_SUCCESS;
		try
		{
			// Call the module start function, if it has one.
			Function* startFunction = getStartFunction(moduleInstance);
			if(startFunction) { invokeFunction(context, startFunction); }

			if(emscriptenInstance)
			{
				// Call the Emscripten global initalizers.
				Emscripten::initializeGlobals(
					emscriptenInstance, context, irModule, moduleInstance);
			}

			// Split the tagged argument values into their types and untagged values.
			std::vector<ValueType> invokeArgTypes;
			std::vector<UntaggedValue> untaggedInvokeArgs;
			for(const Value& arg : invokeArgs)
			{
				invokeArgTypes.push_back(arg.type);
				untaggedInvokeArgs.push_back(arg);
			}

			// Infer the expected type of the function from the number and type of the invoke's
			// arguments and the function's actual result types.
			const FunctionType invokeSig(getFunctionType(function).results(),
										 TypeTuple(invokeArgTypes));

			// Allocate an array to receive the invoke results.
			std::vector<UntaggedValue> untaggedInvokeResults;
			untaggedInvokeResults.resize(invokeSig.results().size());

			// Invoke the function.
			Timing::Timer executionTimer;
			invokeFunction(context,
						   function,
						   invokeSig,
						   untaggedInvokeArgs.data(),
						   untaggedInvokeResults.data());
			Timing::logTimer("Invoked function", executionTimer);

			if(functionName)
			{
				// Convert the untagged result values to tagged values.
				std::vector<Value> invokeResults;
				invokeResults.resize(invokeSig.results().size());
				for(Uptr resultIndex = 0; resultIndex < untaggedInvokeResults.size(); ++resultIndex)
				{
					const ValueType resultType = invokeSig.results()[resultIndex];
					const UntaggedValue& untaggedResult = untaggedInvokeResults[resultIndex];
					invokeResults[resultIndex] = Value(resultType, untaggedResult);
				}

				Log::printf(
					Log::debug, "%s returned: %s\n", functionName, asString(invokeResults).c_str());
			}
			else if(untaggedInvokeResults.size() == 1 && invokeSig.results()[0] == ValueType::i32)
			{
				result = untaggedInvokeResults[0].i32;
			}
		}
		catch(const WASI::ExitException& exitException)
		{
			// If either the WASM or WASI start functions call the WASI exit API, they will throw a
			// WASI::ExitException. Catch it here, and return the exit code.
			result = int(exitException.exitCode);
		}
		catch(const Emscripten::ExitException& exitException)
		{
			// If either the WASM or WASI start functions call the Emscripten exit API, they will
			// throw an Emscripten::ExitException. Catch it here, and return the exit code.
			result = int(exitException.exitCode);
		}

		// Log the peak memory usage.
		Uptr peakMemoryUsage = Platform::getPeakMemoryUsageBytes();
		Log::printf(
			Log::metrics, "Peak memory usage: %" WAVM_PRIuPTR "KiB\n", peakMemoryUsage / 1024);

		return result;
	}

	int runAndCatchRuntimeExceptions(char** argv)
	{
		int result = EXIT_FAILURE;
		Runtime::catchRuntimeExceptions([&result, argv, this]() { result = run(argv); },
										[](Runtime::Exception* exception) {
											// Treat any unhandled exception as a fatal error.
											Errors::fatalf("Runtime exception: %s",
														   describeException(exception).c_str());
										});
		return result;
	}
};

int main(int argc, char** argv)
{
	State state;
	return state.runAndCatchRuntimeExceptions(argv);
}
