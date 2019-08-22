#include <string>
#include <vector>
#include "WAVM/IR/FeatureSpec.h"
#include "WAVM/IR/Module.h"
#include "WAVM/Inline/BasicTypes.h"
#include "WAVM/Inline/CLI.h"
#include "WAVM/Inline/Errors.h"
#include "WAVM/Inline/Serialization.h"
#include "WAVM/Inline/Timing.h"
#include "WAVM/LLVMJIT/LLVMJIT.h"
#include "WAVM/Logging/Logging.h"
#include "WAVM/WASM/WASM.h"
#include "WAVM/WASTParse/WASTParse.h"

using namespace WAVM;
using namespace WAVM::IR;
using namespace WAVM::Runtime;

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

static const char* getOutputFormatHelpText()
{
	return "  unoptimized-llvmir          Unoptimized LLVM IR for the input module.\n"
		   "  optimized-llvmir            Optimized LLVM IR for the input module.\n"
		   "  object                      The target platform's native object file format.\n"
		   "  precompiled-wasm (default)  The original WebAssembly module with object code\n"
		   "                              embedded in the wavm.precompiled_object section.\n";
}

static void showHelp()
{
	LLVMJIT::TargetSpec hostTargetSpec = LLVMJIT::getHostTargetSpec();

	Log::printf(Log::error,
				"Usage: wavm-compile [options] <in.wast|wasm> <output file>\n"
				"  -h|--help                 Display this message\n"
				"  --target-triple <triple>  Set the target triple (default: %s)\n"
				"  --target-cpu <cpu>        Set the target CPU (default: %s)\n"
				"  --enable <feature>        Enable the specified feature. See the list of\n"
				"                            supported features below.\n"
				"  --format=<format>         Specifies the format of the output file. See the\n"
				"                            list of supported output formats below.\n"
				"\n"
				"Output formats:\n"
				"%s"
				"\n"
				"Features:\n"
				"%s"
				"\n",
				hostTargetSpec.triple.c_str(),
				hostTargetSpec.cpu.c_str(),
				getOutputFormatHelpText(),
				getFeatureListHelpText());
}

template<Uptr numPrefixChars>
static bool stringStartsWith(const char* string, const char (&prefix)[numPrefixChars])
{
	return !strncmp(string, prefix, numPrefixChars - 1);
}

enum class OutputFormat
{
	unspecified,
	precompiledModule,
	unoptimizedLLVMIR,
	optimizedLLVMIR,
	object,
};

int main(int argc, char** argv)
{
	const char* inputFilename = nullptr;
	const char* outputFilename = nullptr;
	LLVMJIT::TargetSpec targetSpec = LLVMJIT::getHostTargetSpec();
	IR::FeatureSpec featureSpec;
	OutputFormat outputFormat = OutputFormat::unspecified;
	for(int argIndex = 1; argIndex < argc; ++argIndex)
	{
		if(!strcmp(argv[argIndex], "-h") || !strcmp(argv[argIndex], "--help"))
		{
			showHelp();
			return EXIT_FAILURE;
		}
		else if(!strcmp(argv[argIndex], "--target-triple"))
		{
			if(argIndex + 1 == argc)
			{
				showHelp();
				return EXIT_FAILURE;
			}
			++argIndex;
			targetSpec.triple = argv[argIndex];
		}
		else if(!strcmp(argv[argIndex], "--target-cpu"))
		{
			if(argIndex + 1 == argc)
			{
				showHelp();
				return EXIT_FAILURE;
			}
			++argIndex;
			targetSpec.cpu = argv[argIndex];
		}
		else if(!strcmp(argv[argIndex], "--enable"))
		{
			++argIndex;
			if(argIndex == argc)
			{
				Log::printf(Log::error, "Expected feature name following '--enable'.\n");
				return EXIT_FAILURE;
			}

			if(!parseAndSetFeature(argv[argIndex], featureSpec, true))
			{
				Log::printf(Log::error, "Unknown feature '%s'.\n", argv[argIndex]);
				return EXIT_FAILURE;
			}
		}
		else if(stringStartsWith(argv[argIndex], "--format="))
		{
			if(outputFormat != OutputFormat::unspecified)
			{
				Log::printf(Log::error, "'--format=' may only occur once on the command line.\n");
				return EXIT_FAILURE;
			}

			const char* formatString = argv[argIndex] + strlen("--format=");
			if(!strcmp(formatString, "precompiled-wasm"))
			{ outputFormat = OutputFormat::precompiledModule; }
			else if(!strcmp(formatString, "unoptimized-llvmir"))
			{
				outputFormat = OutputFormat::unoptimizedLLVMIR;
			}
			else if(!strcmp(formatString, "optimized-llvmir"))
			{
				outputFormat = OutputFormat::optimizedLLVMIR;
			}
			else if(!strcmp(formatString, "object"))
			{
				outputFormat = OutputFormat::object;
			}
			else
			{
				Log::printf(Log::error,
							"Invalid output format '%s'. Supported output formats:\n"
							"%s"
							"\n",
							formatString,
							getOutputFormatHelpText());
				return EXIT_FAILURE;
			}
		}
		else if(!inputFilename)
		{
			inputFilename = argv[argIndex];
		}
		else if(!outputFilename)
		{
			outputFilename = argv[argIndex];
		}
		else
		{
			showHelp();
			return EXIT_FAILURE;
		}
	}

	if(!inputFilename || !outputFilename)
	{
		showHelp();
		return EXIT_FAILURE;
	}

	// Validate the target.
	switch(LLVMJIT::validateTarget(targetSpec, featureSpec))
	{
	case LLVMJIT::TargetValidationResult::valid: break;

	case LLVMJIT::TargetValidationResult::invalidTargetSpec:
		Log::printf(Log::error,
					"Target triple (%s) or CPU (%s) is invalid.\n",
					targetSpec.triple.c_str(),
					targetSpec.cpu.c_str());
		return EXIT_FAILURE;
	case LLVMJIT::TargetValidationResult::unsupportedArchitecture:
		Log::printf(Log::error, "WAVM doesn't support the target architecture.\n");
		return EXIT_FAILURE;
	case LLVMJIT::TargetValidationResult::x86CPUDoesNotSupportSSE41:
		Log::printf(Log::error,
					"Target X86 CPU (%s) does not support SSE 4.1, which"
					" WAVM requires for WebAssembly SIMD code.\n",
					targetSpec.cpu.c_str());
		return EXIT_FAILURE;

	default: WAVM_UNREACHABLE();
	};

	if(outputFormat == OutputFormat::unspecified)
	{ outputFormat = OutputFormat::precompiledModule; }

	// Load the module IR.
	IR::Module irModule(featureSpec);
	if(!loadModule(inputFilename, irModule)) { return EXIT_FAILURE; }

	switch(outputFormat)
	{
	case OutputFormat::precompiledModule: {
		// Compile the module to object code.
		std::vector<U8> objectCode = LLVMJIT::compileModule(irModule, targetSpec);

		// Extract the compiled object code and add it to the IR module as a user section.
		irModule.userSections.push_back({"wavm.precompiled_object", objectCode});

		// Serialize the WASM module.
		std::vector<U8> wasmBytes;
		try
		{
			Timing::Timer saveTimer;

			Serialization::ArrayOutputStream stream;
			WASM::serialize(stream, irModule);
			wasmBytes = stream.getBytes();

			Timing::logRatePerSecond(
				"Serialized WASM", saveTimer, wasmBytes.size() / 1024.0 / 1024.0, "MiB");
		}
		catch(Serialization::FatalSerializationException const& exception)
		{
			Log::printf(Log::error,
						"Error serializing WebAssembly binary file:\n%s\n",
						exception.message.c_str());
			return EXIT_FAILURE;
		}

		// Write the serialized data to the output file.
		return saveFile(outputFilename, wasmBytes.data(), wasmBytes.size()) ? EXIT_SUCCESS
																			: EXIT_FAILURE;
	}
	case OutputFormat::object: {
		// Compile the module to object code.
		std::vector<U8> objectCode = LLVMJIT::compileModule(irModule, targetSpec);

		// Write the object code to the output file.
		return saveFile(outputFilename, objectCode.data(), objectCode.size()) ? EXIT_SUCCESS
																			  : EXIT_FAILURE;
	}
	case OutputFormat::optimizedLLVMIR:
	case OutputFormat::unoptimizedLLVMIR: {
		// Compile the module to LLVM IR.
		std::string llvmIR = LLVMJIT::emitLLVMIR(
			irModule, targetSpec, outputFormat == OutputFormat::optimizedLLVMIR);

		// Write the LLVM IR to the output file.
		return saveFile(outputFilename, llvmIR.data(), llvmIR.size()) ? EXIT_SUCCESS : EXIT_FAILURE;
	}

	case OutputFormat::unspecified:
	default: WAVM_UNREACHABLE();
	};
}
