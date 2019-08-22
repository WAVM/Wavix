#include <inttypes.h>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include <vector>

#include "WAVM/IR/IR.h"
#include "WAVM/IR/Module.h"
#include "WAVM/IR/Types.h"
#include "WAVM/Inline/BasicTypes.h"
#include "WAVM/Inline/LEB128.h"
#include "WAVM/Inline/Serialization.h"
#include "WAVM/Logging/Logging.h"

using namespace WAVM;
using namespace WAVM::IR;
using namespace WAVM::Serialization;

enum class NameSubsectionType : U8
{
	module = 0,
	function = 1,
	local = 2,
	label = 3,
	type = 4,
	table = 5,
	memory = 6,
	global = 7,
	elemSegment = 8,
	dataSegment = 9,
	exceptionTypes = 10,
	invalid = 0xff
};

static void deserializeNameMap(InputStream& stream,
							   std::vector<std::string>& outNames,
							   Uptr maxNames)
{
	Uptr numNames = 0;
	serializeVarUInt32(stream, numNames);
	for(Uptr serializedNameIndex = 0; serializedNameIndex < numNames; ++serializedNameIndex)
	{
		Uptr nameIndex = 0;
		serializeVarUInt32(stream, nameIndex);

		std::string nameString;
		serialize(stream, nameString);

		if(nameIndex >= maxNames) { throw FatalSerializationException("out-of-bounds name index"); }

		if(nameIndex >= outNames.size()) { outNames.resize(nameIndex + 1); }

		outNames[nameIndex] = std::move(nameString);
	}
}

static void serializeNameMap(OutputStream& stream, const std::vector<std::string>& outNames)
{
	Uptr numNames = 0;
	for(Uptr nameIndex = 0; nameIndex < outNames.size(); ++nameIndex)
	{
		if(outNames[nameIndex].size()) { ++numNames; }
	}

	serializeVarUInt32(stream, numNames);
	for(Uptr nameIndex = 0; nameIndex < outNames.size(); ++nameIndex)
	{
		if(outNames[nameIndex].size())
		{
			serializeVarUInt32(stream, nameIndex);

			std::string nameString = outNames[nameIndex];
			serialize(stream, nameString);
		}
	}
}

static void deserializeNameSubsection(const Module& module,
									  DisassemblyNames& outNames,
									  InputStream& stream)
{
	U8 subsectionType = (U8)NameSubsectionType::invalid;
	serializeVarUInt7(stream, subsectionType);

	U32 numSubsectionBytes = 0;
	serializeVarUInt32(stream, numSubsectionBytes);

	MemoryInputStream substream(stream.advance(numSubsectionBytes), numSubsectionBytes);
	switch((NameSubsectionType)subsectionType)
	{
	case NameSubsectionType::module: {
		serialize(substream, outNames.moduleName);
		break;
	}
	case NameSubsectionType::function: {
		U32 numFunctionNames = 0;
		serializeVarUInt32(substream, numFunctionNames);
		for(Uptr functionNameIndex = 0; functionNameIndex < numFunctionNames; ++functionNameIndex)
		{
			U32 functionIndex = 0;
			serializeVarUInt32(substream, functionIndex);

			std::string functionName;
			serialize(substream, functionName);

			if(functionIndex < outNames.functions.size())
			{ outNames.functions[functionIndex].name = std::move(functionName); }
		}
		break;
	}
	case NameSubsectionType::local: {
		U32 numFunctionLocalNameMaps = 0;
		serializeVarUInt32(substream, numFunctionLocalNameMaps);
		for(Uptr functionNameIndex = 0; functionNameIndex < numFunctionLocalNameMaps;
			++functionNameIndex)
		{
			U32 functionIndex = 0;
			serializeVarUInt32(substream, functionIndex);

			if(functionIndex < outNames.functions.size())
			{
				deserializeNameMap(substream,
								   outNames.functions[functionIndex].locals,
								   outNames.functions[functionIndex].locals.size());
			}
			else
			{
				Log::printf(
					Log::debug,
					"Invalid WASM binary local name section function index: %u >= %" WAVM_PRIuPTR
					"\n",
					functionIndex,
					Uptr(outNames.functions.size()));
				break;
			}
		}

		break;
	}
	case NameSubsectionType::label: {
		if(!module.featureSpec.extendedNamesSection)
		{
			throw FatalSerializationException(
				"label name subsection requires extendedNamesSection feature");
		}

		U32 numFunctionLabelNameMaps = 0;
		serializeVarUInt32(substream, numFunctionLabelNameMaps);
		for(Uptr functionNameIndex = 0; functionNameIndex < numFunctionLabelNameMaps;
			++functionNameIndex)
		{
			U32 functionIndex = 0;
			serializeVarUInt32(substream, functionIndex);

			if(functionIndex < outNames.functions.size())
			{
				deserializeNameMap(substream,
								   outNames.functions[functionIndex].labels,
								   module.featureSpec.maxLabelsPerFunction);
			}
			else
			{
				Log::printf(
					Log::debug,
					"Invalid WASM binary label name section function index: %u >= %" WAVM_PRIuPTR
					"\n",
					functionIndex,
					Uptr(outNames.functions.size()));
				break;
			}
		}

		break;
	}
	case NameSubsectionType::type:
		if(!module.featureSpec.extendedNamesSection)
		{
			throw FatalSerializationException(
				"type name subsection requires extendedNamesSection feature");
		}
		deserializeNameMap(substream, outNames.types, outNames.types.size());
		break;
	case NameSubsectionType::table:
		if(!module.featureSpec.extendedNamesSection)
		{
			throw FatalSerializationException(
				"table name subsection requires extendedNamesSection feature");
		}
		deserializeNameMap(substream, outNames.tables, outNames.tables.size());
		break;
	case NameSubsectionType::memory:
		if(!module.featureSpec.extendedNamesSection)
		{
			throw FatalSerializationException(
				"memory name subsection requires extendedNamesSection feature");
		}
		deserializeNameMap(substream, outNames.memories, outNames.memories.size());
		break;
	case NameSubsectionType::global:
		if(!module.featureSpec.extendedNamesSection)
		{
			throw FatalSerializationException(
				"global name subsection requires extendedNamesSection feature");
		}
		deserializeNameMap(substream, outNames.globals, outNames.globals.size());
		break;
	case NameSubsectionType::elemSegment:
		if(!module.featureSpec.extendedNamesSection)
		{
			throw FatalSerializationException(
				"elem segment name subsection requires extendedNamesSection feature");
		}
		deserializeNameMap(substream, outNames.elemSegments, outNames.elemSegments.size());
		break;
	case NameSubsectionType::dataSegment:
		if(!module.featureSpec.extendedNamesSection)
		{
			throw FatalSerializationException(
				"data segment name subsection requires extendedNamesSection feature");
		}
		deserializeNameMap(substream, outNames.dataSegments, outNames.dataSegments.size());
		break;
	case NameSubsectionType::exceptionTypes:
		if(!module.featureSpec.extendedNamesSection)
		{
			throw FatalSerializationException(
				"exception type name subsection requires extendedNamesSection feature");
		}
		deserializeNameMap(substream, outNames.exceptionTypes, outNames.exceptionTypes.size());
		break;

	case NameSubsectionType::invalid:
	default:
		Log::printf(Log::debug, "Unknown WASM binary name subsection type: %u\n", subsectionType);
		break;
	};
}

void IR::getDisassemblyNames(const Module& module, DisassemblyNames& outNames)
{
	// Fill in the output with the correct number of blank names.
	for(const auto& functionImport : module.functions.imports)
	{
		DisassemblyNames::Function functionNames;
		functionNames.locals.resize(module.types[functionImport.type.index].params().size());
		outNames.functions.push_back(std::move(functionNames));
	}
	for(Uptr functionDefIndex = 0; functionDefIndex < module.functions.defs.size();
		++functionDefIndex)
	{
		const FunctionDef& functionDef = module.functions.defs[functionDefIndex];
		DisassemblyNames::Function functionNames;
		functionNames.locals.insert(functionNames.locals.begin(),
									module.types[functionDef.type.index].params().size()
										+ functionDef.nonParameterLocalTypes.size(),
									"");
		outNames.functions.push_back(std::move(functionNames));
	}

	outNames.types.insert(outNames.types.end(), module.types.size(), "");
	outNames.tables.insert(outNames.tables.end(), module.tables.size(), "");
	outNames.memories.insert(outNames.memories.end(), module.memories.size(), "");
	outNames.globals.insert(outNames.globals.end(), module.globals.size(), "");
	outNames.elemSegments.insert(outNames.elemSegments.end(), module.elemSegments.size(), "");
	outNames.dataSegments.insert(outNames.dataSegments.end(), module.dataSegments.size(), "");
	outNames.exceptionTypes.insert(outNames.exceptionTypes.end(), module.exceptionTypes.size(), "");

	// Deserialize the name section, if it is present.
	Uptr userSectionIndex = 0;
	if(findUserSection(module, "name", userSectionIndex))
	{
		try
		{
			const UserSection& nameSection = module.userSections[userSectionIndex];
			MemoryInputStream stream(nameSection.data.data(), nameSection.data.size());

			while(stream.capacity()) { deserializeNameSubsection(module, outNames, stream); };
		}
		catch(FatalSerializationException const& exception)
		{
			Log::printf(
				Log::debug,
				"FatalSerializationException while deserializing WASM user name section: %s\n",
				exception.message.c_str());
		}
		catch(std::bad_alloc const&)
		{
			Log::printf(
				Log::debug,
				"Memory allocation failed while deserializing WASM user name section. Input is "
				"likely malformed.");
		}
	}
}

template<typename SerializeBody>
void serializeNameSubsection(OutputStream& stream,
							 NameSubsectionType type,
							 SerializeBody serializeBody)
{
	ArrayOutputStream subsectionStream;
	serializeBody(subsectionStream);
	serialize(stream, *(U8*)&type);
	std::vector<U8> bytes = subsectionStream.getBytes();
	serialize(stream, bytes);
}

void IR::setDisassemblyNames(Module& module, const DisassemblyNames& names)
{
	// Replace an existing name section if one is present, or create a new section.
	Uptr userSectionIndex = 0;
	if(!findUserSection(module, "name", userSectionIndex))
	{
		userSectionIndex = module.userSections.size();
		module.userSections.push_back({"name", {}});
	}

	ArrayOutputStream stream;

	// Module name
	serializeNameSubsection(
		stream, NameSubsectionType::module, [&names](OutputStream& subsectionStream) {
			std::string moduleName = names.moduleName;
			serialize(subsectionStream, moduleName);
		});

	// Function names
	serializeNameSubsection(
		stream, NameSubsectionType::function, [&names](OutputStream& subsectionStream) {
			Uptr numFunctionNames = names.functions.size();
			serializeVarUInt32(subsectionStream, numFunctionNames);
			for(Uptr functionIndex = 0; functionIndex < names.functions.size(); ++functionIndex)
			{
				serializeVarUInt32(subsectionStream, functionIndex);
				std::string functionName = names.functions[functionIndex].name;
				serialize(subsectionStream, functionName);
			}
		});

	// Local names.
	serializeNameSubsection(
		stream, NameSubsectionType::local, [&names](OutputStream& subsectionStream) {
			Uptr numFunctionNames = names.functions.size();
			serializeVarUInt32(subsectionStream, numFunctionNames);
			for(Uptr functionIndex = 0; functionIndex < names.functions.size(); ++functionIndex)
			{
				serializeVarUInt32(subsectionStream, functionIndex);
				serializeNameMap(subsectionStream, names.functions[functionIndex].locals);
			}
		});

	if(module.featureSpec.extendedNamesSection)
	{
		// Label names.
		serializeNameSubsection(
			stream, NameSubsectionType::label, [&names](OutputStream& subsectionStream) {
				Uptr numFunctionNames = names.functions.size();
				serializeVarUInt32(subsectionStream, numFunctionNames);
				for(Uptr functionIndex = 0; functionIndex < names.functions.size(); ++functionIndex)
				{
					serializeVarUInt32(subsectionStream, functionIndex);
					serializeNameMap(subsectionStream, names.functions[functionIndex].labels);
				}
			});

		// Type names
		serializeNameSubsection(
			stream, NameSubsectionType::type, [&names](OutputStream& subsectionStream) {
				serializeNameMap(subsectionStream, names.types);
			});

		// Table names
		serializeNameSubsection(
			stream, NameSubsectionType::table, [&names](OutputStream& subsectionStream) {
				serializeNameMap(subsectionStream, names.tables);
			});

		// Memory names
		serializeNameSubsection(
			stream, NameSubsectionType::memory, [&names](OutputStream& subsectionStream) {
				serializeNameMap(subsectionStream, names.memories);
			});

		//  Global names
		serializeNameSubsection(
			stream, NameSubsectionType::global, [&names](OutputStream& subsectionStream) {
				serializeNameMap(subsectionStream, names.globals);
			});

		// Elem segments
		serializeNameSubsection(
			stream, NameSubsectionType::elemSegment, [&names](OutputStream& subsectionStream) {
				serializeNameMap(subsectionStream, names.elemSegments);
			});

		// Data segments
		serializeNameSubsection(
			stream, NameSubsectionType::dataSegment, [&names](OutputStream& subsectionStream) {
				serializeNameMap(subsectionStream, names.dataSegments);
			});

		// Exception types
		serializeNameSubsection(
			stream, NameSubsectionType::exceptionTypes, [&names](OutputStream& subsectionStream) {
				serializeNameMap(subsectionStream, names.exceptionTypes);
			});
	}

	module.userSections[userSectionIndex].data = stream.getBytes();
}
