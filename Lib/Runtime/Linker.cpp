#include "WAVM/Runtime/Linker.h"
#include "WAVM/IR/Module.h"
#include "WAVM/IR/Validate.h"
#include "WAVM/Inline/Assert.h"
#include "WAVM/Logging/Logging.h"
#include "WAVM/Runtime/Runtime.h"

using namespace WAVM;
using namespace WAVM::IR;
using namespace WAVM::Runtime;

Runtime::StubResolver::StubResolver(Compartment* inCompartment,
									Resolver& inInnerResolver,
									FunctionBehavior inFunctionBehavior,
									bool inLogErrorOnStubGeneration)
: compartment(inCompartment)
, innerResolver(inInnerResolver)
, functionBehavior(inFunctionBehavior)
, logErrorOnStubGeneration(inLogErrorOnStubGeneration)
{
}

bool Runtime::StubResolver::resolve(const std::string& moduleName,
									const std::string& exportName,
									IR::ExternType type,
									Runtime::Object*& outObject)
{
	if(!innerResolver.resolve(moduleName, exportName, type, outObject))
	{
		if(logErrorOnStubGeneration)
		{
			Log::printf(Log::error,
						"Generated stub for missing import %s.%s : %s\n",
						moduleName.c_str(),
						exportName.c_str(),
						asString(type).c_str());
		}

		// If the import couldn't be resolved, stub it in.
		switch(type.kind)
		{
		case IR::ExternKind::function:
		{
			Serialization::ArrayOutputStream codeStream;
			OperatorEncoderStream encoder(codeStream);
			if(functionBehavior == FunctionBehavior::trap)
			{
				// Generate a function body that just uses the unreachable op to fault if called.
				encoder.unreachable();
			}
			else
			{
				// Generate a function body that just returns some reasonable zero value.
				for(IR::ValueType result : asFunctionType(type).results())
				{
					switch(result)
					{
					case IR::ValueType::i32: encoder.i32_const({0}); break;
					case IR::ValueType::i64: encoder.i64_const({0}); break;
					case IR::ValueType::f32: encoder.f32_const({0.0f}); break;
					case IR::ValueType::f64: encoder.f64_const({0.0}); break;
					case IR::ValueType::v128: encoder.v128_const({V128{{0, 0}}}); break;
					case IR::ValueType::anyref:
					case IR::ValueType::funcref: encoder.ref_null(); break;

					case IR::ValueType::none:
					case IR::ValueType::any:
					case IR::ValueType::nullref:
					default: WAVM_UNREACHABLE();
					};
				}
			}
			encoder.end();

			// Generate a module for the stub function.
			IR::Module stubIRModule;
			DisassemblyNames stubModuleNames;
			stubIRModule.types.push_back(asFunctionType(type));
			stubIRModule.functions.defs.push_back({{0}, {}, std::move(codeStream.getBytes()), {}});
			stubIRModule.exports.push_back({"importStub", IR::ExternKind::function, 0});
			stubModuleNames.functions.push_back({"importStub: " + exportName, {}, {}});
			IR::setDisassemblyNames(stubIRModule, stubModuleNames);
			IR::validatePreCodeSections(stubIRModule);
			IR::validatePostCodeSections(stubIRModule);

			// Instantiate the module and return the stub function instance.
			auto stubModule = compileModule(stubIRModule);
			auto stubModuleInstance = instantiateModule(compartment, stubModule, {}, "importStub");
			outObject = getInstanceExport(stubModuleInstance, "importStub");
			break;
		}
		case IR::ExternKind::memory:
		{
			outObject = asObject(
				Runtime::createMemory(compartment, asMemoryType(type), std::string(exportName)));
			break;
		}
		case IR::ExternKind::table:
		{
			outObject = asObject(Runtime::createTable(
				compartment, asTableType(type), nullptr, std::string(exportName)));
			break;
		}
		case IR::ExternKind::global:
		{
			outObject = asObject(Runtime::createGlobal(compartment, asGlobalType(type)));
			break;
		}
		case IR::ExternKind::exceptionType:
		{
			outObject = asObject(
				Runtime::createExceptionType(compartment, asExceptionType(type), "importStub"));
			break;
		}

		case IR::ExternKind::invalid:
		default: WAVM_UNREACHABLE();
		};
	}

	return true;
}

template<typename Type, typename ResolvedType>
static void linkImport(const IR::Module& module,
					   const Import<Type>& import,
					   ResolvedType resolvedType,
					   Resolver& resolver,
					   LinkResult& linkResult)
{
	// Ask the resolver for a value for this import.
	Object* importValue;
	if(resolver.resolve(import.moduleName, import.exportName, resolvedType, importValue))
	{
		// Sanity check that the resolver returned an object of the right type.
		wavmAssert(isA(importValue, resolvedType));
		linkResult.resolvedImports.push_back(importValue);
	}
	else
	{
		linkResult.missingImports.push_back({import.moduleName, import.exportName, resolvedType});
		linkResult.resolvedImports.push_back(nullptr);
	}
}

LinkResult Runtime::linkModule(const IR::Module& module, Resolver& resolver)
{
	LinkResult linkResult;
	wavmAssert(module.imports.size()
			   == module.functions.imports.size() + module.tables.imports.size()
					  + module.memories.imports.size() + module.globals.imports.size()
					  + module.exceptionTypes.imports.size());
	for(const auto& kindIndex : module.imports)
	{
		switch(kindIndex.kind)
		{
		case ExternKind::function:
		{
			const auto& functionImport = module.functions.imports[kindIndex.index];
			linkImport(module,
					   functionImport,
					   module.types[functionImport.type.index],
					   resolver,
					   linkResult);
			break;
		}
		case ExternKind::table:
		{
			const auto& tableImport = module.tables.imports[kindIndex.index];
			linkImport(module, tableImport, tableImport.type, resolver, linkResult);
			break;
		}
		case ExternKind::memory:
		{
			const auto& memoryImport = module.memories.imports[kindIndex.index];
			linkImport(module, memoryImport, memoryImport.type, resolver, linkResult);
			break;
		}
		case ExternKind::global:
		{
			const auto& globalImport = module.globals.imports[kindIndex.index];
			linkImport(module, globalImport, globalImport.type, resolver, linkResult);
			break;
		}
		case ExternKind::exceptionType:
		{
			const auto& exceptionTypeImport = module.exceptionTypes.imports[kindIndex.index];
			linkImport(module, exceptionTypeImport, exceptionTypeImport.type, resolver, linkResult);
			break;
		}

		case ExternKind::invalid:
		default: WAVM_UNREACHABLE();
		};
	}

	linkResult.success = linkResult.missingImports.size() == 0;
	return linkResult;
}
