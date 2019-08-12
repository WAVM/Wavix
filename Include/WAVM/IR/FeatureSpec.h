#pragma once

#include "WAVM/Inline/BasicTypes.h"

namespace WAVM { namespace IR {
	struct FeatureSpec
	{
		// A feature flag for the MVP, just so the MVP operators can reference it as the required
		// feature flag.
		const bool mvp = true;

		// Proposed standard extensions that are likely to be standardized without further changes.
		bool importExportMutableGlobals = true;
		bool nonTrappingFloatToInt = true;
		bool extendedSignExtension = true;

		// Proposed standard extensions
		bool simd = true;
		bool atomics = true;
		bool exceptionHandling = true;
		bool multipleResultsAndBlockParams = true;
		bool bulkMemoryOperations = true;
		bool referenceTypes = true;
		bool extendedNamesSection = true;
		bool quotedNamesInTextFormat = true; // Enabled by default for everything but wavm-disas,
											 // where a command-line flag is required to enable it
											 // to ensure the default output uses standard syntax.

		// WAVM-specific extensions
		bool sharedTables = true;
		bool requireSharedFlagForAtomicOperators = false; // (true is standard)
		bool allowLegacyOperatorNames = true;

		Uptr maxLocals = 65536;
		Uptr maxLabelsPerFunction = UINTPTR_MAX;
		Uptr maxDataSegments = UINTPTR_MAX;

		FeatureSpec(bool enablePreStandardizationFeatures = false)
		{
			setPreStandardizationFeatures(enablePreStandardizationFeatures);
		}

		void setPreStandardizationFeatures(bool enablePreStandardizationFeatures)
		{
			simd = enablePreStandardizationFeatures;
			atomics = enablePreStandardizationFeatures;
			exceptionHandling = enablePreStandardizationFeatures;
			multipleResultsAndBlockParams = enablePreStandardizationFeatures;
			bulkMemoryOperations = enablePreStandardizationFeatures;
			referenceTypes = enablePreStandardizationFeatures;
		}
	};

	IR_API const char* getFeatureListHelpText();
	IR_API bool parseAndSetFeature(const char* featureName, FeatureSpec& featureSpec, bool enable);
}}
