#pragma once

#include "WAVM/IR/Types.h"
#include "WAVM/Inline/BasicTypes.h"

#include <string.h>

namespace WAVM { namespace Runtime {
	struct Object;
	struct Function;
}}

namespace WAVM { namespace IR {
	// A runtime value of any type.
	struct UntaggedValue
	{
		union
		{
			I32 i32;
			U32 u32;
			I64 i64;
			U64 u64;
			F32 f32;
			F64 f64;
			V128 v128;
			U8 bytes[16];
			Runtime::Object* object;
			Runtime::Function* function;
		};

		UntaggedValue(I32 inI32) { i32 = inI32; }
		UntaggedValue(I64 inI64) { i64 = inI64; }
		UntaggedValue(U32 inU32) { u32 = inU32; }
		UntaggedValue(U64 inU64) { u64 = inU64; }
		UntaggedValue(F32 inF32) { f32 = inF32; }
		UntaggedValue(F64 inF64) { f64 = inF64; }
		UntaggedValue(V128 inV128) { v128 = inV128; }
		UntaggedValue(Runtime::Object* inObject) { object = inObject; }
		UntaggedValue(Runtime::Function* inFunction) { function = inFunction; }
		UntaggedValue() { memset(this, 0, sizeof(*this)); }
	};

	// A boxed value: may hold any value that can be passed to a function invoked through the
	// runtime.
	struct Value : UntaggedValue
	{
		ValueType type;

		Value(I32 inI32) : UntaggedValue(inI32), type(ValueType::i32) {}
		Value(I64 inI64) : UntaggedValue(inI64), type(ValueType::i64) {}
		Value(U32 inU32) : UntaggedValue(inU32), type(ValueType::i32) {}
		Value(U64 inU64) : UntaggedValue(inU64), type(ValueType::i64) {}
		Value(F32 inF32) : UntaggedValue(inF32), type(ValueType::f32) {}
		Value(F64 inF64) : UntaggedValue(inF64), type(ValueType::f64) {}
		Value(const V128& inV128) : UntaggedValue(inV128), type(ValueType::v128) {}
		Value(std::nullptr_t) : UntaggedValue((Runtime::Object*)nullptr), type(ValueType::nullref)
		{
		}
		Value(Runtime::Object* inObject) : UntaggedValue(inObject), type(ValueType::anyref) {}
		Value(Runtime::Function* inFunction) : UntaggedValue(inFunction), type(ValueType::funcref)
		{
		}
		Value(ValueType inType, UntaggedValue inValue) : UntaggedValue(inValue), type(inType) {}
		Value() : type(ValueType::none) {}

		friend std::string asString(const Value& value)
		{
			switch(value.type)
			{
			case ValueType::i32: return "i32.const " + asString(value.i32);
			case ValueType::i64: return "i64.const " + asString(value.i64);
			case ValueType::f32: return "f32.const " + asString(value.f32);
			case ValueType::f64: return "f64.const " + asString(value.f64);
			case ValueType::v128: return "v128.const " + asString(value.v128);
			case ValueType::anyref:
			case ValueType::funcref: {
				// buffer needs 27 characters:
				// (anyref|funcref) 0xHHHHHHHHHHHHHHHH\0
				char buffer[27];
				snprintf(buffer,
						 sizeof(buffer),
						 "%s 0x%.16" WAVM_PRIxPTR,
						 value.type == ValueType::anyref ? "anyref" : "funcref",
						 reinterpret_cast<Uptr>(value.object));
				return std::string(buffer);
			}
			case ValueType::nullref: return "ref.null";

			case ValueType::none:
			case ValueType::any:
			default: WAVM_UNREACHABLE();
			}
		}

		friend bool operator==(const Value& left, const Value& right)
		{
			if(left.type != right.type)
			{
				return isReferenceType(left.type) && isReferenceType(right.type)
					   && left.object == right.object;
			}
			switch(left.type)
			{
			case ValueType::none: return true;
			case ValueType::i32:
			case ValueType::f32: return left.i32 == right.i32;
			case ValueType::i64:
			case ValueType::f64: return left.i64 == right.i64;
			case ValueType::v128:
				return left.v128.u64[0] == right.v128.u64[0]
					   && left.v128.u64[1] == right.v128.u64[1];
			case ValueType::anyref:
			case ValueType::funcref: return left.object == right.object;
			case ValueType::nullref: return true;
			case ValueType::any:
			default: WAVM_UNREACHABLE();
			};
		}

		friend bool operator!=(const Value& left, const Value& right) { return !(left == right); }
	};

	// A boxed value: may hold any value that can be returned from a function invoked through the
	// runtime.
	struct ValueTuple
	{
		std::vector<Value> values;

		ValueTuple(ValueType inType, UntaggedValue inValue) : values({Value(inType, inValue)}) {}
		ValueTuple(TypeTuple types, UntaggedValue* inValues)
		{
			values.reserve(types.size());
			for(ValueType type : types) { values.push_back(Value(type, *inValues++)); }
		}
		ValueTuple(const Value& inValue) : values({inValue}) {}
		ValueTuple() {}

		Uptr size() const { return values.size(); }
		Value& operator[](Uptr index) { return values[index]; }
		const Value& operator[](Uptr index) const { return values[index]; }

		friend std::string asString(const ValueTuple& valueTuple)
		{
			std::string result = "(";
			for(Uptr elementIndex = 0; elementIndex < valueTuple.size(); ++elementIndex)
			{
				if(elementIndex != 0) { result += ", "; }
				result += asString(valueTuple[elementIndex]);
			}
			result += ")";
			return result;
		}

		friend bool operator==(const ValueTuple& left, const ValueTuple& right)
		{
			if(left.size() != right.size()) { return false; }
			for(Uptr valueIndex = 0; valueIndex < left.size(); ++valueIndex)
			{
				if(left[valueIndex] != right[valueIndex]) { return false; }
			}
			return true;
		}

		friend bool operator!=(const ValueTuple& left, const ValueTuple& right)
		{
			return !(left == right);
		}
	};
}}
