// IWYU pragma: private, include "IR/Operators.h"
// Don't include this file directly; include Operators.h instead

// clang-format off

// Maps a non-parametric operator signature to a function type.
#define NONE                                    WAVM::IR::FunctionType()
#define LOAD(resultTypeId)                      WAVM::IR::FunctionType({WAVM::IR::ValueType::resultTypeId}, {WAVM::IR::ValueType::i32                                                                                })
#define STORE(valueTypeId)                      WAVM::IR::FunctionType({},                                  {WAVM::IR::ValueType::i32,           WAVM::IR::ValueType::valueTypeId                                    })
#define NULLARY(resultTypeId)                   WAVM::IR::FunctionType({WAVM::IR::ValueType::resultTypeId}, {                                                                                                        })
#define BINARY(operandTypeId, resultTypeId)     WAVM::IR::FunctionType({WAVM::IR::ValueType::resultTypeId}, {WAVM::IR::ValueType::operandTypeId, WAVM::IR::ValueType::operandTypeId                                  })
#define UNARY(operandTypeId, resultTypeId)      WAVM::IR::FunctionType({WAVM::IR::ValueType::resultTypeId}, {WAVM::IR::ValueType::operandTypeId                                                                      })
#define VECTORSELECT(vectorTypeId)              WAVM::IR::FunctionType({WAVM::IR::ValueType::vectorTypeId}, {WAVM::IR::ValueType::vectorTypeId,  WAVM::IR::ValueType::vectorTypeId, WAVM::IR::ValueType::vectorTypeId})
#define V_VS(vectorTypeId, scalarTypeId)        WAVM::IR::FunctionType({WAVM::IR::ValueType::vectorTypeId}, {WAVM::IR::ValueType::vectorTypeId,  WAVM::IR::ValueType::scalarTypeId                                   })
#define COMPAREEXCHANGE(valueTypeId)            WAVM::IR::FunctionType({WAVM::IR::ValueType::valueTypeId},  {WAVM::IR::ValueType::i32,           WAVM::IR::ValueType::valueTypeId,  WAVM::IR::ValueType::valueTypeId })
#define WAIT(valueTypeId)                       WAVM::IR::FunctionType({WAVM::IR::ValueType::i32},          {WAVM::IR::ValueType::i32,           WAVM::IR::ValueType::valueTypeId,  WAVM::IR::ValueType::i64         })
#define ATOMICRMW(valueTypeId)                  WAVM::IR::FunctionType({WAVM::IR::ValueType::valueTypeId},  {WAVM::IR::ValueType::i32,           WAVM::IR::ValueType::valueTypeId                                    })
#define BULKCOPY                                WAVM::IR::FunctionType({},                                  {WAVM::IR::ValueType::i32,           WAVM::IR::ValueType::i32,          WAVM::IR::ValueType::i32         })

// Enumerate the WebAssembly operators

#define ENUM_CONTROL_OPERATORS(visitOp)                                                                                                                     \
	visitOp(0x0002, block              , "block"                            , ControlStructureImm       , PARAMETRIC           , mvp                    )   \
	visitOp(0x0003, loop               , "loop"                             , ControlStructureImm       , PARAMETRIC           , mvp                    )   \
	visitOp(0x0004, if_                , "if"                               , ControlStructureImm       , PARAMETRIC           , mvp                    )   \
	visitOp(0x0005, else_              , "else"                             , NoImm                     , PARAMETRIC           , mvp                    )   \
	visitOp(0x000b, end                , "end"                              , NoImm                     , PARAMETRIC           , mvp                    )   \
	visitOp(0xfb02, try_               , "try"                              , ControlStructureImm       , PARAMETRIC           , exceptionHandling      )   \
	visitOp(0xfb03, catch_             , "catch"                            , ExceptionTypeImm          , PARAMETRIC           , exceptionHandling      )   \
	visitOp(0xfb04, catch_all          , "catch_all"                        , NoImm                     , PARAMETRIC           , exceptionHandling      )

#define ENUM_PARAMETRIC_OPERATORS(visitOp)                                                                                                                  \
/* Control flow                                                                                                                                          */ \
	visitOp(0x0000, unreachable        , "unreachable"                      , NoImm                     , PARAMETRIC           , mvp                    )   \
	visitOp(0x000c, br                 , "br"                               , BranchImm                 , PARAMETRIC           , mvp                    )   \
	visitOp(0x000d, br_if              , "br_if"                            , BranchImm                 , PARAMETRIC           , mvp                    )   \
	visitOp(0x000e, br_table           , "br_table"                         , BranchTableImm            , PARAMETRIC           , mvp                    )   \
	visitOp(0x000f, return_            , "return"                           , NoImm                     , PARAMETRIC           , mvp                    )   \
	visitOp(0x0010, call               , "call"                             , FunctionImm               , PARAMETRIC           , mvp                    )   \
	visitOp(0x0011, call_indirect      , "call_indirect"                    , CallIndirectImm           , PARAMETRIC           , mvp                    )   \
/* Stack manipulation                                                                                                                                    */ \
	visitOp(0x001a, drop               , "drop"                             , NoImm                     , PARAMETRIC           , mvp                    )   \
	visitOp(0x001b, select             , "select"                           , NoImm                     , PARAMETRIC           , mvp                    )   \
/* Variables                                                                                                                                             */ \
	visitOp(0x0020, local_get          , "local.get"                        , GetOrSetVariableImm<false>, PARAMETRIC           , mvp                    )   \
	visitOp(0x0021, local_set          , "local.set"                        , GetOrSetVariableImm<false>, PARAMETRIC           , mvp                    )   \
	visitOp(0x0022, local_tee          , "local.tee"                        , GetOrSetVariableImm<false>, PARAMETRIC           , mvp                    )   \
	visitOp(0x0023, global_get         , "global.get"                       , GetOrSetVariableImm<true> , PARAMETRIC           , mvp                    )   \
	visitOp(0x0024, global_set         , "global.set"                       , GetOrSetVariableImm<true> , PARAMETRIC           , mvp                    )   \
/* Table access                                                                                                                                          */ \
	visitOp(0x0025, table_get          , "table.get"                        , TableImm                  , PARAMETRIC           , referenceTypes         )   \
	visitOp(0x0026, table_set          , "table.set"                        , TableImm                  , PARAMETRIC           , referenceTypes         )   \
	visitOp(0xfc0f, table_grow         , "table.grow"                       , TableImm                  , PARAMETRIC           , referenceTypes         )   \
	visitOp(0xfc11, table_fill         , "table.fill"                       , TableImm                  , PARAMETRIC           , referenceTypes         )   \
/* Exceptions                                                                                                                                            */ \
	visitOp(0xfb00, throw_             , "throw"                            , ExceptionTypeImm          , PARAMETRIC           , exceptionHandling      )   \
	visitOp(0xfb01, rethrow            , "rethrow"                          , RethrowImm                , PARAMETRIC           , exceptionHandling      )

#define ENUM_NONCONTROL_NONPARAMETRIC_OPERATORS(visitOp)                                                                                                    \
	visitOp(0x0001, nop                , "nop"                              , NoImm                     , NONE                 , mvp                    )   \
/* Memory                                                                                                                                                */ \
	visitOp(0x0028, i32_load           , "i32.load"                         , LoadOrStoreImm<2>         , LOAD(i32)            , mvp                    )   \
	visitOp(0x0029, i64_load           , "i64.load"                         , LoadOrStoreImm<3>         , LOAD(i64)            , mvp                    )   \
	visitOp(0x002a, f32_load           , "f32.load"                         , LoadOrStoreImm<2>         , LOAD(f32)            , mvp                    )   \
	visitOp(0x002b, f64_load           , "f64.load"                         , LoadOrStoreImm<3>         , LOAD(f64)            , mvp                    )   \
	visitOp(0x002c, i32_load8_s        , "i32.load8_s"                      , LoadOrStoreImm<0>         , LOAD(i32)            , mvp                    )   \
	visitOp(0x002d, i32_load8_u        , "i32.load8_u"                      , LoadOrStoreImm<0>         , LOAD(i32)            , mvp                    )   \
	visitOp(0x002e, i32_load16_s       , "i32.load16_s"                     , LoadOrStoreImm<1>         , LOAD(i32)            , mvp                    )   \
	visitOp(0x002f, i32_load16_u       , "i32.load16_u"                     , LoadOrStoreImm<1>         , LOAD(i32)            , mvp                    )   \
	visitOp(0x0030, i64_load8_s        , "i64.load8_s"                      , LoadOrStoreImm<0>         , LOAD(i64)            , mvp                    )   \
	visitOp(0x0031, i64_load8_u        , "i64.load8_u"                      , LoadOrStoreImm<0>         , LOAD(i64)            , mvp                    )   \
	visitOp(0x0032, i64_load16_s       , "i64.load16_s"                     , LoadOrStoreImm<1>         , LOAD(i64)            , mvp                    )   \
	visitOp(0x0033, i64_load16_u       , "i64.load16_u"                     , LoadOrStoreImm<1>         , LOAD(i64)            , mvp                    )   \
	visitOp(0x0034, i64_load32_s       , "i64.load32_s"                     , LoadOrStoreImm<2>         , LOAD(i64)            , mvp                    )   \
	visitOp(0x0035, i64_load32_u       , "i64.load32_u"                     , LoadOrStoreImm<2>         , LOAD(i64)            , mvp                    )   \
	visitOp(0x0036, i32_store          , "i32.store"                        , LoadOrStoreImm<2>         , STORE(i32)           , mvp                    )   \
	visitOp(0x0037, i64_store          , "i64.store"                        , LoadOrStoreImm<3>         , STORE(i64)           , mvp                    )   \
	visitOp(0x0038, f32_store          , "f32.store"                        , LoadOrStoreImm<2>         , STORE(f32)           , mvp                    )   \
	visitOp(0x0039, f64_store          , "f64.store"                        , LoadOrStoreImm<3>         , STORE(f64)           , mvp                    )   \
	visitOp(0x003a, i32_store8         , "i32.store8"                       , LoadOrStoreImm<0>         , STORE(i32)           , mvp                    )   \
	visitOp(0x003b, i32_store16        , "i32.store16"                      , LoadOrStoreImm<1>         , STORE(i32)           , mvp                    )   \
	visitOp(0x003c, i64_store8         , "i64.store8"                       , LoadOrStoreImm<0>         , STORE(i64)           , mvp                    )   \
	visitOp(0x003d, i64_store16        , "i64.store16"                      , LoadOrStoreImm<1>         , STORE(i64)           , mvp                    )   \
	visitOp(0x003e, i64_store32        , "i64.store32"                      , LoadOrStoreImm<2>         , STORE(i64)           , mvp                    )   \
	visitOp(0x003f, memory_size        , "memory.size"                      , MemoryImm                 , NULLARY(i32)         , mvp                    )   \
	visitOp(0x0040, memory_grow        , "memory.grow"                      , MemoryImm                 , UNARY(i32,i32)       , mvp                    )   \
/* Literals                                                                                                                                              */ \
	visitOp(0x0041, i32_const          , "i32.const"                        , LiteralImm<I32>           , NULLARY(i32)         , mvp                    )   \
	visitOp(0x0042, i64_const          , "i64.const"                        , LiteralImm<I64>           , NULLARY(i64)         , mvp                    )   \
	visitOp(0x0043, f32_const          , "f32.const"                        , LiteralImm<F32>           , NULLARY(f32)         , mvp                    )   \
	visitOp(0x0044, f64_const          , "f64.const"                        , LiteralImm<F64>           , NULLARY(f64)         , mvp                    )   \
/* Comparisons                                                                                                                                           */ \
	visitOp(0x0045, i32_eqz            , "i32.eqz"                          , NoImm                     , UNARY(i32,i32)       , mvp                    )   \
	visitOp(0x0046, i32_eq             , "i32.eq"                           , NoImm                     , BINARY(i32,i32)      , mvp                    )   \
	visitOp(0x0047, i32_ne             , "i32.ne"                           , NoImm                     , BINARY(i32,i32)      , mvp                    )   \
	visitOp(0x0048, i32_lt_s           , "i32.lt_s"                         , NoImm                     , BINARY(i32,i32)      , mvp                    )   \
	visitOp(0x0049, i32_lt_u           , "i32.lt_u"                         , NoImm                     , BINARY(i32,i32)      , mvp                    )   \
	visitOp(0x004a, i32_gt_s           , "i32.gt_s"                         , NoImm                     , BINARY(i32,i32)      , mvp                    )   \
	visitOp(0x004b, i32_gt_u           , "i32.gt_u"                         , NoImm                     , BINARY(i32,i32)      , mvp                    )   \
	visitOp(0x004c, i32_le_s           , "i32.le_s"                         , NoImm                     , BINARY(i32,i32)      , mvp                    )   \
	visitOp(0x004d, i32_le_u           , "i32.le_u"                         , NoImm                     , BINARY(i32,i32)      , mvp                    )   \
	visitOp(0x004e, i32_ge_s           , "i32.ge_s"                         , NoImm                     , BINARY(i32,i32)      , mvp                    )   \
	visitOp(0x004f, i32_ge_u           , "i32.ge_u"                         , NoImm                     , BINARY(i32,i32)      , mvp                    )   \
	visitOp(0x0050, i64_eqz            , "i64.eqz"                          , NoImm                     , UNARY(i64,i32)       , mvp                    )   \
	visitOp(0x0051, i64_eq             , "i64.eq"                           , NoImm                     , BINARY(i64,i32)      , mvp                    )   \
	visitOp(0x0052, i64_ne             , "i64.ne"                           , NoImm                     , BINARY(i64,i32)      , mvp                    )   \
	visitOp(0x0053, i64_lt_s           , "i64.lt_s"                         , NoImm                     , BINARY(i64,i32)      , mvp                    )   \
	visitOp(0x0054, i64_lt_u           , "i64.lt_u"                         , NoImm                     , BINARY(i64,i32)      , mvp                    )   \
	visitOp(0x0055, i64_gt_s           , "i64.gt_s"                         , NoImm                     , BINARY(i64,i32)      , mvp                    )   \
	visitOp(0x0056, i64_gt_u           , "i64.gt_u"                         , NoImm                     , BINARY(i64,i32)      , mvp                    )   \
	visitOp(0x0057, i64_le_s           , "i64.le_s"                         , NoImm                     , BINARY(i64,i32)      , mvp                    )   \
	visitOp(0x0058, i64_le_u           , "i64.le_u"                         , NoImm                     , BINARY(i64,i32)      , mvp                    )   \
	visitOp(0x0059, i64_ge_s           , "i64.ge_s"                         , NoImm                     , BINARY(i64,i32)      , mvp                    )   \
	visitOp(0x005a, i64_ge_u           , "i64.ge_u"                         , NoImm                     , BINARY(i64,i32)      , mvp                    )   \
	visitOp(0x005b, f32_eq             , "f32.eq"                           , NoImm                     , BINARY(f32,i32)      , mvp                    )   \
	visitOp(0x005c, f32_ne             , "f32.ne"                           , NoImm                     , BINARY(f32,i32)      , mvp                    )   \
	visitOp(0x005d, f32_lt             , "f32.lt"                           , NoImm                     , BINARY(f32,i32)      , mvp                    )   \
	visitOp(0x005e, f32_gt             , "f32.gt"                           , NoImm                     , BINARY(f32,i32)      , mvp                    )   \
	visitOp(0x005f, f32_le             , "f32.le"                           , NoImm                     , BINARY(f32,i32)      , mvp                    )   \
	visitOp(0x0060, f32_ge             , "f32.ge"                           , NoImm                     , BINARY(f32,i32)      , mvp                    )   \
	visitOp(0x0061, f64_eq             , "f64.eq"                           , NoImm                     , BINARY(f64,i32)      , mvp                    )   \
	visitOp(0x0062, f64_ne             , "f64.ne"                           , NoImm                     , BINARY(f64,i32)      , mvp                    )   \
	visitOp(0x0063, f64_lt             , "f64.lt"                           , NoImm                     , BINARY(f64,i32)      , mvp                    )   \
	visitOp(0x0064, f64_gt             , "f64.gt"                           , NoImm                     , BINARY(f64,i32)      , mvp                    )   \
	visitOp(0x0065, f64_le             , "f64.le"                           , NoImm                     , BINARY(f64,i32)      , mvp                    )   \
	visitOp(0x0066, f64_ge             , "f64.ge"                           , NoImm                     , BINARY(f64,i32)      , mvp                    )   \
/* i32 arithmetic                                                                                                                                        */ \
	visitOp(0x0067, i32_clz            , "i32.clz"                          , NoImm                     , UNARY(i32,i32)       , mvp                    )   \
	visitOp(0x0068, i32_ctz            , "i32.ctz"                          , NoImm                     , UNARY(i32,i32)       , mvp                    )   \
	visitOp(0x0069, i32_popcnt         , "i32.popcnt"                       , NoImm                     , UNARY(i32,i32)       , mvp                    )   \
	visitOp(0x006a, i32_add            , "i32.add"                          , NoImm                     , BINARY(i32,i32)      , mvp                    )   \
	visitOp(0x006b, i32_sub            , "i32.sub"                          , NoImm                     , BINARY(i32,i32)      , mvp                    )   \
	visitOp(0x006c, i32_mul            , "i32.mul"                          , NoImm                     , BINARY(i32,i32)      , mvp                    )   \
	visitOp(0x006d, i32_div_s          , "i32.div_s"                        , NoImm                     , BINARY(i32,i32)      , mvp                    )   \
	visitOp(0x006e, i32_div_u          , "i32.div_u"                        , NoImm                     , BINARY(i32,i32)      , mvp                    )   \
	visitOp(0x006f, i32_rem_s          , "i32.rem_s"                        , NoImm                     , BINARY(i32,i32)      , mvp                    )   \
	visitOp(0x0070, i32_rem_u          , "i32.rem_u"                        , NoImm                     , BINARY(i32,i32)      , mvp                    )   \
	visitOp(0x0071, i32_and_           , "i32.and"                          , NoImm                     , BINARY(i32,i32)      , mvp                    )   \
	visitOp(0x0072, i32_or_            , "i32.or"                           , NoImm                     , BINARY(i32,i32)      , mvp                    )   \
	visitOp(0x0073, i32_xor_           , "i32.xor"                          , NoImm                     , BINARY(i32,i32)      , mvp                    )   \
	visitOp(0x0074, i32_shl            , "i32.shl"                          , NoImm                     , BINARY(i32,i32)      , mvp                    )   \
	visitOp(0x0075, i32_shr_s          , "i32.shr_s"                        , NoImm                     , BINARY(i32,i32)      , mvp                    )   \
	visitOp(0x0076, i32_shr_u          , "i32.shr_u"                        , NoImm                     , BINARY(i32,i32)      , mvp                    )   \
	visitOp(0x0077, i32_rotl           , "i32.rotl"                         , NoImm                     , BINARY(i32,i32)      , mvp                    )   \
	visitOp(0x0078, i32_rotr           , "i32.rotr"                         , NoImm                     , BINARY(i32,i32)      , mvp                    )   \
/* i64 arithmetic                                                                                                                                        */ \
	visitOp(0x0079, i64_clz            , "i64.clz"                          , NoImm                     , UNARY(i64,i64)       , mvp                    )   \
	visitOp(0x007a, i64_ctz            , "i64.ctz"                          , NoImm                     , UNARY(i64,i64)       , mvp                    )   \
	visitOp(0x007b, i64_popcnt         , "i64.popcnt"                       , NoImm                     , UNARY(i64,i64)       , mvp                    )   \
	visitOp(0x007c, i64_add            , "i64.add"                          , NoImm                     , BINARY(i64,i64)      , mvp                    )   \
	visitOp(0x007d, i64_sub            , "i64.sub"                          , NoImm                     , BINARY(i64,i64)      , mvp                    )   \
	visitOp(0x007e, i64_mul            , "i64.mul"                          , NoImm                     , BINARY(i64,i64)      , mvp                    )   \
	visitOp(0x007f, i64_div_s          , "i64.div_s"                        , NoImm                     , BINARY(i64,i64)      , mvp                    )   \
	visitOp(0x0080, i64_div_u          , "i64.div_u"                        , NoImm                     , BINARY(i64,i64)      , mvp                    )   \
	visitOp(0x0081, i64_rem_s          , "i64.rem_s"                        , NoImm                     , BINARY(i64,i64)      , mvp                    )   \
	visitOp(0x0082, i64_rem_u          , "i64.rem_u"                        , NoImm                     , BINARY(i64,i64)      , mvp                    )   \
	visitOp(0x0083, i64_and_           , "i64.and"                          , NoImm                     , BINARY(i64,i64)      , mvp                    )   \
	visitOp(0x0084, i64_or_            , "i64.or"                           , NoImm                     , BINARY(i64,i64)      , mvp                    )   \
	visitOp(0x0085, i64_xor_           , "i64.xor"                          , NoImm                     , BINARY(i64,i64)      , mvp                    )   \
	visitOp(0x0086, i64_shl            , "i64.shl"                          , NoImm                     , BINARY(i64,i64)      , mvp                    )   \
	visitOp(0x0087, i64_shr_s          , "i64.shr_s"                        , NoImm                     , BINARY(i64,i64)      , mvp                    )   \
	visitOp(0x0088, i64_shr_u          , "i64.shr_u"                        , NoImm                     , BINARY(i64,i64)      , mvp                    )   \
	visitOp(0x0089, i64_rotl           , "i64.rotl"                         , NoImm                     , BINARY(i64,i64)      , mvp                    )   \
	visitOp(0x008a, i64_rotr           , "i64.rotr"                         , NoImm                     , BINARY(i64,i64)      , mvp                    )   \
/* f32 arithmetic                                                                                                                                        */ \
	visitOp(0x008b, f32_abs            , "f32.abs"                          , NoImm                     , UNARY(f32,f32)       , mvp                    )   \
	visitOp(0x008c, f32_neg            , "f32.neg"                          , NoImm                     , UNARY(f32,f32)       , mvp                    )   \
	visitOp(0x008d, f32_ceil           , "f32.ceil"                         , NoImm                     , UNARY(f32,f32)       , mvp                    )   \
	visitOp(0x008e, f32_floor          , "f32.floor"                        , NoImm                     , UNARY(f32,f32)       , mvp                    )   \
	visitOp(0x008f, f32_trunc          , "f32.trunc"                        , NoImm                     , UNARY(f32,f32)       , mvp                    )   \
	visitOp(0x0090, f32_nearest        , "f32.nearest"                      , NoImm                     , UNARY(f32,f32)       , mvp                    )   \
	visitOp(0x0091, f32_sqrt           , "f32.sqrt"                         , NoImm                     , UNARY(f32,f32)       , mvp                    )   \
	visitOp(0x0092, f32_add            , "f32.add"                          , NoImm                     , BINARY(f32,f32)      , mvp                    )   \
	visitOp(0x0093, f32_sub            , "f32.sub"                          , NoImm                     , BINARY(f32,f32)      , mvp                    )   \
	visitOp(0x0094, f32_mul            , "f32.mul"                          , NoImm                     , BINARY(f32,f32)      , mvp                    )   \
	visitOp(0x0095, f32_div            , "f32.div"                          , NoImm                     , BINARY(f32,f32)      , mvp                    )   \
	visitOp(0x0096, f32_min            , "f32.min"                          , NoImm                     , BINARY(f32,f32)      , mvp                    )   \
	visitOp(0x0097, f32_max            , "f32.max"                          , NoImm                     , BINARY(f32,f32)      , mvp                    )   \
	visitOp(0x0098, f32_copysign       , "f32.copysign"                     , NoImm                     , BINARY(f32,f32)      , mvp                    )   \
/* f64 arithmetic                                                                                                                                        */ \
	visitOp(0x0099, f64_abs            , "f64.abs"                          , NoImm                     , UNARY(f64,f64)       , mvp                    )   \
	visitOp(0x009a, f64_neg            , "f64.neg"                          , NoImm                     , UNARY(f64,f64)       , mvp                    )   \
	visitOp(0x009b, f64_ceil           , "f64.ceil"                         , NoImm                     , UNARY(f64,f64)       , mvp                    )   \
	visitOp(0x009c, f64_floor          , "f64.floor"                        , NoImm                     , UNARY(f64,f64)       , mvp                    )   \
	visitOp(0x009d, f64_trunc          , "f64.trunc"                        , NoImm                     , UNARY(f64,f64)       , mvp                    )   \
	visitOp(0x009e, f64_nearest        , "f64.nearest"                      , NoImm                     , UNARY(f64,f64)       , mvp                    )   \
	visitOp(0x009f, f64_sqrt           , "f64.sqrt"                         , NoImm                     , UNARY(f64,f64)       , mvp                    )   \
	visitOp(0x00a0, f64_add            , "f64.add"                          , NoImm                     , BINARY(f64,f64)      , mvp                    )   \
	visitOp(0x00a1, f64_sub            , "f64.sub"                          , NoImm                     , BINARY(f64,f64)      , mvp                    )   \
	visitOp(0x00a2, f64_mul            , "f64.mul"                          , NoImm                     , BINARY(f64,f64)      , mvp                    )   \
	visitOp(0x00a3, f64_div            , "f64.div"                          , NoImm                     , BINARY(f64,f64)      , mvp                    )   \
	visitOp(0x00a4, f64_min            , "f64.min"                          , NoImm                     , BINARY(f64,f64)      , mvp                    )   \
	visitOp(0x00a5, f64_max            , "f64.max"                          , NoImm                     , BINARY(f64,f64)      , mvp                    )   \
	visitOp(0x00a6, f64_copysign       , "f64.copysign"                     , NoImm                     , BINARY(f64,f64)      , mvp                    )   \
/* Conversions                                                                                                                                           */ \
	visitOp(0x00a7, i32_wrap_i64       , "i32.wrap_i64"                     , NoImm                     , UNARY(i64,i32)       , mvp                    )   \
	visitOp(0x00a8, i32_trunc_f32_s    , "i32.trunc_f32_s"                  , NoImm                     , UNARY(f32,i32)       , mvp                    )   \
	visitOp(0x00a9, i32_trunc_f32_u    , "i32.trunc_f32_u"                  , NoImm                     , UNARY(f32,i32)       , mvp                    )   \
	visitOp(0x00aa, i32_trunc_f64_s    , "i32.trunc_f64_s"                  , NoImm                     , UNARY(f64,i32)       , mvp                    )   \
	visitOp(0x00ab, i32_trunc_f64_u    , "i32.trunc_f64_u"                  , NoImm                     , UNARY(f64,i32)       , mvp                    )   \
	visitOp(0x00ac, i64_extend_i32_s   , "i64.extend_i32_s"                 , NoImm                     , UNARY(i32,i64)       , mvp                    )   \
	visitOp(0x00ad, i64_extend_i32_u   , "i64.extend_i32_u"                 , NoImm                     , UNARY(i32,i64)       , mvp                    )   \
	visitOp(0x00ae, i64_trunc_f32_s    , "i64.trunc_f32_s"                  , NoImm                     , UNARY(f32,i64)       , mvp                    )   \
	visitOp(0x00af, i64_trunc_f32_u    , "i64.trunc_f32_u"                  , NoImm                     , UNARY(f32,i64)       , mvp                    )   \
	visitOp(0x00b0, i64_trunc_f64_s    , "i64.trunc_f64_s"                  , NoImm                     , UNARY(f64,i64)       , mvp                    )   \
	visitOp(0x00b1, i64_trunc_f64_u    , "i64.trunc_f64_u"                  , NoImm                     , UNARY(f64,i64)       , mvp                    )   \
	visitOp(0x00b2, f32_convert_i32_s  , "f32.convert_i32_s"                , NoImm                     , UNARY(i32,f32)       , mvp                    )   \
	visitOp(0x00b3, f32_convert_i32_u  , "f32.convert_i32_u"                , NoImm                     , UNARY(i32,f32)       , mvp                    )   \
	visitOp(0x00b4, f32_convert_i64_s  , "f32.convert_i64_s"                , NoImm                     , UNARY(i64,f32)       , mvp                    )   \
	visitOp(0x00b5, f32_convert_i64_u  , "f32.convert_i64_u"                , NoImm                     , UNARY(i64,f32)       , mvp                    )   \
	visitOp(0x00b6, f32_demote_f64     , "f32.demote_f64"                   , NoImm                     , UNARY(f64,f32)       , mvp                    )   \
	visitOp(0x00b7, f64_convert_i32_s  , "f64.convert_i32_s"                , NoImm                     , UNARY(i32,f64)       , mvp                    )   \
	visitOp(0x00b8, f64_convert_i32_u  , "f64.convert_i32_u"                , NoImm                     , UNARY(i32,f64)       , mvp                    )   \
	visitOp(0x00b9, f64_convert_i64_s  , "f64.convert_i64_s"                , NoImm                     , UNARY(i64,f64)       , mvp                    )   \
	visitOp(0x00ba, f64_convert_i64_u  , "f64.convert_i64_u"                , NoImm                     , UNARY(i64,f64)       , mvp                    )   \
	visitOp(0x00bb, f64_promote_f32    , "f64.promote_f32"                  , NoImm                     , UNARY(f32,f64)       , mvp                    )   \
	visitOp(0x00bc, i32_reinterpret_f32, "i32.reinterpret_f32"              , NoImm                     , UNARY(f32,i32)       , mvp                    )   \
	visitOp(0x00bd, i64_reinterpret_f64, "i64.reinterpret_f64"              , NoImm                     , UNARY(f64,i64)       , mvp                    )   \
	visitOp(0x00be, f32_reinterpret_i32, "f32.reinterpret_i32"              , NoImm                     , UNARY(i32,f32)       , mvp                    )   \
	visitOp(0x00bf, f64_reinterpret_i64, "f64.reinterpret_i64"              , NoImm                     , UNARY(i64,f64)       , mvp                    )   \
/* 8- and 16-bit sign extension operators                                                                                                                */ \
	visitOp(0x00c0, i32_extend8_s             , "i32.extend8_s"             , NoImm                     , UNARY(i32,i32)       , extendedSignExtension  )   \
	visitOp(0x00c1, i32_extend16_s            , "i32.extend16_s"            , NoImm                     , UNARY(i32,i32)       , extendedSignExtension  )   \
	visitOp(0x00c2, i64_extend8_s             , "i64.extend8_s"             , NoImm                     , UNARY(i64,i64)       , extendedSignExtension  )   \
	visitOp(0x00c3, i64_extend16_s            , "i64.extend16_s"            , NoImm                     , UNARY(i64,i64)       , extendedSignExtension  )   \
	visitOp(0x00c4, i64_extend32_s            , "i64.extend32_s"            , NoImm                     , UNARY(i64,i64)       , extendedSignExtension  )   \
/* Reference type operators                                                                                                                              */ \
	visitOp(0x00d0, ref_null                  , "ref.null"                  , NoImm                     , NULLARY(nullref)     , referenceTypes         )   \
	visitOp(0x00d1, ref_is_null               , "ref.is_null"               , NoImm                     , UNARY(anyref,i32)    , referenceTypes         )   \
	visitOp(0x00d2, ref_func                  , "ref.func"                  , FunctionImm               , NULLARY(funcref)     , functionRefInstruction )   \
/* Saturating float->int truncation operators                                                                                                            */ \
	visitOp(0xfc00, i32_trunc_sat_f32_s       , "i32.trunc_sat_f32_s"       , NoImm                     , UNARY(f32,i32)       , nonTrappingFloatToInt  )   \
	visitOp(0xfc01, i32_trunc_sat_f32_u       , "i32.trunc_sat_f32_u"       , NoImm                     , UNARY(f32,i32)       , nonTrappingFloatToInt  )   \
	visitOp(0xfc02, i32_trunc_sat_f64_s       , "i32.trunc_sat_f64_s"       , NoImm                     , UNARY(f64,i32)       , nonTrappingFloatToInt  )   \
	visitOp(0xfc03, i32_trunc_sat_f64_u       , "i32.trunc_sat_f64_u"       , NoImm                     , UNARY(f64,i32)       , nonTrappingFloatToInt  )   \
	visitOp(0xfc04, i64_trunc_sat_f32_s       , "i64.trunc_sat_f32_s"       , NoImm                     , UNARY(f32,i64)       , nonTrappingFloatToInt  )   \
	visitOp(0xfc05, i64_trunc_sat_f32_u       , "i64.trunc_sat_f32_u"       , NoImm                     , UNARY(f32,i64)       , nonTrappingFloatToInt  )   \
	visitOp(0xfc06, i64_trunc_sat_f64_s       , "i64.trunc_sat_f64_s"       , NoImm                     , UNARY(f64,i64)       , nonTrappingFloatToInt  )   \
	visitOp(0xfc07, i64_trunc_sat_f64_u       , "i64.trunc_sat_f64_u"       , NoImm                     , UNARY(f64,i64)       , nonTrappingFloatToInt  )   \
/* Bulk memory/table operators                                                                                                                                 */ \
	visitOp(0xfc08, memory_init               , "memory.init"               , DataSegmentAndMemImm      , BULKCOPY             , bulkMemoryOperations   )   \
	visitOp(0xfc09, data_drop                 , "data.drop"                 , DataSegmentImm            , NONE                 , bulkMemoryOperations   )   \
	visitOp(0xfc0a, memory_copy               , "memory.copy"               , MemoryCopyImm             , BULKCOPY             , bulkMemoryOperations   )   \
	visitOp(0xfc0b, memory_fill               , "memory.fill"               , MemoryImm                 , BULKCOPY             , bulkMemoryOperations   )   \
	visitOp(0xfc0c, table_init                , "table.init"                , ElemSegmentAndTableImm    , BULKCOPY             , bulkMemoryOperations   )   \
	visitOp(0xfc0d, elem_drop                 , "elem.drop"                 , ElemSegmentImm            , NONE                 , bulkMemoryOperations   )   \
	visitOp(0xfc0e, table_copy                , "table.copy"                , TableCopyImm              , BULKCOPY             , bulkMemoryOperations   )   \
	visitOp(0xfc10, table_size                , "table.size"                , TableImm                  , NULLARY(i32)         , referenceTypes         )   \
/* v128 operators                                                                                                                                        */ \
	visitOp(0xfd00, v128_load                 , "v128.load"                 , LoadOrStoreImm<4>         , LOAD(v128)           , simd                   )   \
	visitOp(0xfd01, v128_store                , "v128.store"                , LoadOrStoreImm<4>         , STORE(v128)          , simd                   )   \
	visitOp(0xfd02, v128_const                , "v128.const"                , LiteralImm<V128>          , NULLARY(v128)        , simd                   )   \
/*  visitOp(0xfd03,                           ,                             ,                           ,                      , simd                   )*/ \
	visitOp(0xfd04, i8x16_splat               , "i8x16.splat"               , NoImm                     , UNARY(i32,v128)      , simd                   )   \
	visitOp(0xfd05, i8x16_extract_lane_s      , "i8x16.extract_lane_s"      , LaneIndexImm<16>          , UNARY(v128,i32)      , simd                   )   \
	visitOp(0xfd06, i8x16_extract_lane_u      , "i8x16.extract_lane_u"      , LaneIndexImm<16>          , UNARY(v128,i32)      , simd                   )   \
	visitOp(0xfd07, i8x16_replace_lane        , "i8x16.replace_lane"        , LaneIndexImm<16>          , V_VS(v128,i32)       , simd                   )   \
	visitOp(0xfd08, i16x8_splat               , "i16x8.splat"               , NoImm                     , UNARY(i32,v128)      , simd                   )   \
	visitOp(0xfd09, i16x8_extract_lane_s      , "i16x8.extract_lane_s"      , LaneIndexImm<8>           , UNARY(v128,i32)      , simd                   )   \
	visitOp(0xfd0a, i16x8_extract_lane_u      , "i16x8.extract_lane_u"      , LaneIndexImm<8>           , UNARY(v128,i32)      , simd                   )   \
	visitOp(0xfd0b, i16x8_replace_lane        , "i16x8.replace_lane"        , LaneIndexImm<8>           , V_VS(v128,i32)       , simd                   )   \
	visitOp(0xfd0c, i32x4_splat               , "i32x4.splat"               , NoImm                     , UNARY(i32,v128)      , simd                   )   \
	visitOp(0xfd0d, i32x4_extract_lane        , "i32x4.extract_lane"        , LaneIndexImm<4>           , UNARY(v128,i32)      , simd                   )   \
	visitOp(0xfd0e, i32x4_replace_lane        , "i32x4.replace_lane"        , LaneIndexImm<4>           , V_VS(v128,i32)       , simd                   )   \
	visitOp(0xfd0f, i64x2_splat               , "i64x2.splat"               , NoImm                     , UNARY(i64,v128)      , simd                   )   \
	visitOp(0xfd10, i64x2_extract_lane        , "i64x2.extract_lane"        , LaneIndexImm<2>           , UNARY(v128,i64)      , simd                   )   \
	visitOp(0xfd11, i64x2_replace_lane        , "i64x2.replace_lane"        , LaneIndexImm<2>           , V_VS(v128,i64)       , simd                   )   \
	visitOp(0xfd12, f32x4_splat               , "f32x4.splat"               , NoImm                     , UNARY(f32,v128)      , simd                   )   \
	visitOp(0xfd13, f32x4_extract_lane        , "f32x4.extract_lane"        , LaneIndexImm<4>           , UNARY(v128,f32)      , simd                   )   \
	visitOp(0xfd14, f32x4_replace_lane        , "f32x4.replace_lane"        , LaneIndexImm<4>           , V_VS(v128,f32)       , simd                   )   \
	visitOp(0xfd15, f64x2_splat               , "f64x2.splat"               , NoImm                     , UNARY(f64,v128)      , simd                   )   \
	visitOp(0xfd16, f64x2_extract_lane        , "f64x2.extract_lane"        , LaneIndexImm<2>           , UNARY(v128,f64)      , simd                   )   \
	visitOp(0xfd17, f64x2_replace_lane        , "f64x2.replace_lane"        , LaneIndexImm<2>           , V_VS(v128,f64)       , simd                   )   \
/* v128 comparisons                                                                                                                                      */ \
	visitOp(0xfd18, i8x16_eq                  , "i8x16.eq"                  , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfd19, i8x16_ne                  , "i8x16.ne"                  , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfd1a, i8x16_lt_s                , "i8x16.lt_s"                , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfd1b, i8x16_lt_u                , "i8x16.lt_u"                , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfd1c, i8x16_gt_s                , "i8x16.gt_s"                , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfd1d, i8x16_gt_u                , "i8x16.gt_u"                , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfd1e, i8x16_le_s                , "i8x16.le_s"                , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfd1f, i8x16_le_u                , "i8x16.le_u"                , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfd20, i8x16_ge_s                , "i8x16.ge_s"                , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfd21, i8x16_ge_u                , "i8x16.ge_u"                , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfd22, i16x8_eq                  , "i16x8.eq"                  , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfd23, i16x8_ne                  , "i16x8.ne"                  , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfd24, i16x8_lt_s                , "i16x8.lt_s"                , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfd25, i16x8_lt_u                , "i16x8.lt_u"                , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfd26, i16x8_gt_s                , "i16x8.gt_s"                , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfd27, i16x8_gt_u                , "i16x8.gt_u"                , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfd28, i16x8_le_s                , "i16x8.le_s"                , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfd29, i16x8_le_u                , "i16x8.le_u"                , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfd2a, i16x8_ge_s                , "i16x8.ge_s"                , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfd2b, i16x8_ge_u                , "i16x8.ge_u"                , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfd2c, i32x4_eq                  , "i32x4.eq"                  , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfd2d, i32x4_ne                  , "i32x4.ne"                  , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfd2e, i32x4_lt_s                , "i32x4.lt_s"                , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfd2f, i32x4_lt_u                , "i32x4.lt_u"                , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfd30, i32x4_gt_s                , "i32x4.gt_s"                , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfd31, i32x4_gt_u                , "i32x4.gt_u"                , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfd32, i32x4_le_s                , "i32x4.le_s"                , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfd33, i32x4_le_u                , "i32x4.le_u"                , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfd34, i32x4_ge_s                , "i32x4.ge_s"                , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfd35, i32x4_ge_u                , "i32x4.ge_u"                , NoImm                     , BINARY(v128,v128)    , simd                   )   \
/*	visitOp(0xfd36, i64x2_eq                  , "i64x2.eq"                  , NoImm                     , BINARY(v128,v128)    , simd                   )*/ \
/*	visitOp(0xfd37, i64x2_ne                  , "i64x2.ne"                  , NoImm                     , BINARY(v128,v128)    , simd                   )*/ \
/*	visitOp(0xfd38, i64x2_lt_s                , "i64x2.lt_u"                , NoImm                     , BINARY(v128,v128)    , simd                   )*/ \
/*	visitOp(0xfd39, i64x2_lt_u                , "i64x2.lt_u"                , NoImm                     , BINARY(v128,v128)    , simd                   )*/ \
/*	visitOp(0xfd3a, i64x2_gt_s                , "i64x2.gt_u"                , NoImm                     , BINARY(v128,v128)    , simd                   )*/ \
/*	visitOp(0xfd3b, i64x2_gt_u                , "i64x2.gt_u"                , NoImm                     , BINARY(v128,v128)    , simd                   )*/ \
/*	visitOp(0xfd3c, i64x2_le_s                , "i64x2.le_u"                , NoImm                     , BINARY(v128,v128)    , simd                   )*/ \
/*	visitOp(0xfd3d, i64x2_le_u                , "i64x2.le_u"                , NoImm                     , BINARY(v128,v128)    , simd                   )*/ \
/*	visitOp(0xfd3e, i64x2_ge_s                , "i64x2.ge_u"                , NoImm                     , BINARY(v128,v128)    , simd                   )*/ \
/*	visitOp(0xfd3f, i64x2_ge_u                , "i64x2.ge_u"                , NoImm                     , BINARY(v128,v128)    , simd                   )*/ \
	visitOp(0xfd40, f32x4_eq                  , "f32x4.eq"                  , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfd41, f32x4_ne                  , "f32x4.ne"                  , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfd42, f32x4_lt                  , "f32x4.lt"                  , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfd43, f32x4_gt                  , "f32x4.gt"                  , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfd44, f32x4_le                  , "f32x4.le"                  , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfd45, f32x4_ge                  , "f32x4.ge"                  , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfd46, f64x2_eq                  , "f64x2.eq"                  , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfd47, f64x2_ne                  , "f64x2.ne"                  , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfd48, f64x2_lt                  , "f64x2.lt"                  , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfd49, f64x2_gt                  , "f64x2.gt"                  , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfd4a, f64x2_le                  , "f64x2.le"                  , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfd4b, f64x2_ge                  , "f64x2.ge"                  , NoImm                     , BINARY(v128,v128)    , simd                   )   \
/* v128 bitwise                                                                                                                                          */ \
	visitOp(0xfd4c, v128_not                  , "v128.not"                  , NoImm                     , UNARY(v128,v128)     , simd                   )   \
	visitOp(0xfd4d, v128_and                  , "v128.and"                  , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfd4e, v128_or                   , "v128.or"                   , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfd4f, v128_xor                  , "v128.xor"                  , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfd50, v128_bitselect            , "v128.bitselect"            , NoImm                     , VECTORSELECT(v128)   , simd                   )   \
/* v128 integer arithmetic                                                                                                                               */ \
	visitOp(0xfd51, i8x16_neg                 , "i8x16.neg"                 , NoImm                     , UNARY(v128,v128)     , simd                   )   \
	visitOp(0xfd52, i8x16_any_true            , "i8x16.any_true"            , NoImm                     , UNARY(v128,i32)      , simd                   )   \
	visitOp(0xfd53, i8x16_all_true            , "i8x16.all_true"            , NoImm                     , UNARY(v128,i32)      , simd                   )   \
	visitOp(0xfd54, i8x16_shl                 , "i8x16.shl"                 , NoImm                     , V_VS(v128,i32)       , simd                   )   \
	visitOp(0xfd55, i8x16_shr_s               , "i8x16.shr_s"               , NoImm                     , V_VS(v128,i32)       , simd                   )   \
	visitOp(0xfd56, i8x16_shr_u               , "i8x16.shr_u"               , NoImm                     , V_VS(v128,i32)       , simd                   )   \
	visitOp(0xfd57, i8x16_add                 , "i8x16.add"                 , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfd58, i8x16_add_saturate_s      , "i8x16.add_saturate_s"      , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfd59, i8x16_add_saturate_u      , "i8x16.add_saturate_u"      , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfd5a, i8x16_sub                 , "i8x16.sub"                 , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfd5b, i8x16_sub_saturate_s      , "i8x16.sub_saturate_s"      , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfd5c, i8x16_sub_saturate_u      , "i8x16.sub_saturate_u"      , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfd5d, i8x16_mul                 , "i8x16.mul"                 , NoImm                     , BINARY(v128,v128)    , simd                   )   \
/*	visitOp(0xfd5e,                           ,                             ,                           ,                      , simd                   )*/ \
/*	visitOp(0xfd5f,                           ,                             ,                           ,                      , simd                   )*/ \
/*	visitOp(0xfd60,                           ,                             ,                           ,                      , simd                   )*/ \
/*	visitOp(0xfd61,                           ,                             ,                           ,                      , simd                   )*/ \
	visitOp(0xfd62, i16x8_neg                 , "i16x8.neg"                 , NoImm                     , UNARY(v128,v128)     , simd                   )   \
	visitOp(0xfd63, i16x8_any_true            , "i16x8.any_true"            , NoImm                     , UNARY(v128,i32)      , simd                   )   \
	visitOp(0xfd64, i16x8_all_true            , "i16x8.all_true"            , NoImm                     , UNARY(v128,i32)      , simd                   )   \
	visitOp(0xfd65, i16x8_shl                 , "i16x8.shl"                 , NoImm                     , V_VS(v128,i32)       , simd                   )   \
	visitOp(0xfd66, i16x8_shr_s               , "i16x8.shr_s"               , NoImm                     , V_VS(v128,i32)       , simd                   )   \
	visitOp(0xfd67, i16x8_shr_u               , "i16x8.shr_u"               , NoImm                     , V_VS(v128,i32)       , simd                   )   \
	visitOp(0xfd68, i16x8_add                 , "i16x8.add"                 , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfd69, i16x8_add_saturate_s      , "i16x8.add_saturate_s"      , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfd6a, i16x8_add_saturate_u      , "i16x8.add_saturate_u"      , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfd6b, i16x8_sub                 , "i16x8.sub"                 , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfd6c, i16x8_sub_saturate_s      , "i16x8.sub_saturate_s"      , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfd6d, i16x8_sub_saturate_u      , "i16x8.sub_saturate_u"      , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfd6e, i16x8_mul                 , "i16x8.mul"                 , NoImm                     , BINARY(v128,v128)    , simd                   )   \
/*	visitOp(0xfd6f,                           ,                             ,                           ,                      , simd                   )*/ \
/*	visitOp(0xfd70,                           ,                             ,                           ,                      , simd                   )*/ \
/*	visitOp(0xfd71,                           ,                             ,                           ,                      , simd                   )*/ \
/*	visitOp(0xfd72,                           ,                             ,                           ,                      , simd                   )*/ \
	visitOp(0xfd73, i32x4_neg                 , "i32x4.neg"                 , NoImm                     , UNARY(v128,v128)     , simd                   )   \
	visitOp(0xfd74, i32x4_any_true            , "i32x4.any_true"            , NoImm                     , UNARY(v128,i32)      , simd                   )   \
	visitOp(0xfd75, i32x4_all_true            , "i32x4.all_true"            , NoImm                     , UNARY(v128,i32)      , simd                   )   \
	visitOp(0xfd76, i32x4_shl                 , "i32x4.shl"                 , NoImm                     , V_VS(v128,i32)       , simd                   )   \
	visitOp(0xfd77, i32x4_shr_s               , "i32x4.shr_s"               , NoImm                     , V_VS(v128,i32)       , simd                   )   \
	visitOp(0xfd78, i32x4_shr_u               , "i32x4.shr_u"               , NoImm                     , V_VS(v128,i32)       , simd                   )   \
	visitOp(0xfd79, i32x4_add                 , "i32x4.add"                 , NoImm                     , BINARY(v128,v128)    , simd                   )   \
/*	visitOp(0xfd7a, i32x4_add_saturate_s      , "i32x4.add_saturate_s"      , NoImm                     , BINARY(v128,v128)    , simd                   )*/ \
/*	visitOp(0xfd7b, i32x4_add_saturate_u      , "i32x4.add_saturate_u"      , NoImm                     , BINARY(v128,v128)    , simd                   )*/ \
	visitOp(0xfd7c, i32x4_sub                 , "i32x4.sub"                 , NoImm                     , BINARY(v128,v128)    , simd                   )   \
/*	visitOp(0xfd7d, i32x4_sub_saturate_s      , "i32x4.sub_saturate_s"      , NoImm                     , BINARY(v128,v128)    , simd                   )*/ \
/*	visitOp(0xfd7e, i32x4_sub_saturate_u      , "i32x4.sub_saturate_u"      , NoImm                     , BINARY(v128,v128)    , simd                   )*/ \
	visitOp(0xfd7f, i32x4_mul                 , "i32x4.mul"                 , NoImm                     , BINARY(v128,v128)    , simd                   )   \
/*	visitOp(0xfd80,                           ,                             ,                           ,                      , simd                   )*/ \
/*	visitOp(0xfd81,                           ,                             ,                           ,                      , simd                   )*/ \
/*	visitOp(0xfd82,                           ,                             ,                           ,                      , simd                   )*/ \
/*	visitOp(0xfd83,                           ,                             ,                           ,                      , simd                   )*/ \
	visitOp(0xfd84, i64x2_neg                 , "i64x2.neg"                 , NoImm                     , UNARY(v128,v128)     , simd                   )   \
	visitOp(0xfd85, i64x2_any_true            , "i64x2.any_true"            , NoImm                     , UNARY(v128,i32)      , simd                   )   \
	visitOp(0xfd86, i64x2_all_true            , "i64x2.all_true"            , NoImm                     , UNARY(v128,i32)      , simd                   )   \
	visitOp(0xfd87, i64x2_shl                 , "i64x2.shl"                 , NoImm                     , V_VS(v128,i32)       , simd                   )   \
	visitOp(0xfd88, i64x2_shr_s               , "i64x2.shr_s"               , NoImm                     , V_VS(v128,i32)       , simd                   )   \
	visitOp(0xfd89, i64x2_shr_u               , "i64x2.shr_u"               , NoImm                     , V_VS(v128,i32)       , simd                   )   \
	visitOp(0xfd8a, i64x2_add                 , "i64x2.add"                 , NoImm                     , BINARY(v128,v128)    , simd                   )   \
/*	visitOp(0xfd8b, i64x2_add_saturate_s      , "i64x2.add_saturate_s"      , NoImm                     , BINARY(v128,v128)    , simd                   )*/ \
/*	visitOp(0xfd8c, i64x2_add_saturate_u      , "i64x2.add_saturate_u"      , NoImm                     , BINARY(v128,v128)    , simd                   )*/ \
	visitOp(0xfd8d, i64x2_sub                 , "i64x2.sub"                 , NoImm                     , BINARY(v128,v128)    , simd                   )   \
/*	visitOp(0xfd8e, i64x2_sub_saturate_s      , "i64x2.sub_saturate_s"      , NoImm                     , BINARY(v128,v128)    , simd                   )*/ \
/*	visitOp(0xfd8f, i64x2_sub_saturate_u      , "i64x2.sub_saturate_u"      , NoImm                     , BINARY(v128,v128)    , simd                   )*/ \
/*	visitOp(0xfd90, i64x2_mul                 , "i64x2.mul"                 , NoImm                     , BINARY(v128,v128)    , simd                   )*/ \
/*	visitOp(0xfd91,                           ,                             ,                           ,                      , simd                   )*/ \
/*	visitOp(0xfd92,                           ,                             ,                           ,                      , simd                   )*/ \
/*	visitOp(0xfd93,                           ,                             ,                           ,                      , simd                   )*/ \
/*	visitOp(0xfd94,                           ,                             ,                           ,                      , simd                   )*/ \
/* v128 floating-point arithmetic                                                                                                                        */ \
	visitOp(0xfd95, f32x4_abs                 , "f32x4.abs"                 , NoImm                     , UNARY(v128,v128)     , simd                   )   \
	visitOp(0xfd96, f32x4_neg                 , "f32x4.neg"                 , NoImm                     , UNARY(v128,v128)     , simd                   )   \
	visitOp(0xfd97, f32x4_sqrt                , "f32x4.sqrt"                , NoImm                     , UNARY(v128,v128)     , simd                   )   \
/*	visitOp(0xfd98,                           ,                             ,                           ,                      , simd                   )*/ \
/*	visitOp(0xfd99,                           ,                             ,                           ,                      , simd                   )*/ \
	visitOp(0xfd9a, f32x4_add                 , "f32x4.add"                 , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfd9b, f32x4_sub                 , "f32x4.sub"                 , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfd9c, f32x4_mul                 , "f32x4.mul"                 , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfd9d, f32x4_div                 , "f32x4.div"                 , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfd9e, f32x4_min                 , "f32x4.min"                 , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfd9f, f32x4_max                 , "f32x4.max"                 , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfda0, f64x2_abs                 , "f64x2.abs"                 , NoImm                     , UNARY(v128,v128)     , simd                   )   \
	visitOp(0xfda1, f64x2_neg                 , "f64x2.neg"                 , NoImm                     , UNARY(v128,v128)     , simd                   )   \
	visitOp(0xfda2, f64x2_sqrt                , "f64x2.sqrt"                , NoImm                     , UNARY(v128,v128)     , simd                   )   \
/*	visitOp(0xfda3,                           ,                             ,                           ,                      , simd                   )*/ \
/*	visitOp(0xfda4,                           ,                             ,                           ,                      , simd                   )*/ \
	visitOp(0xfda5, f64x2_add                 , "f64x2.add"                 , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfda6, f64x2_sub                 , "f64x2.sub"                 , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfda7, f64x2_mul                 , "f64x2.mul"                 , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfda8, f64x2_div                 , "f64x2.div"                 , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfda9, f64x2_min                 , "f64x2.min"                 , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfdaa, f64x2_max                 , "f64x2.max"                 , NoImm                     , BINARY(v128,v128)    , simd                   )   \
/* v128 conversions                                                                                                                                      */ \
	visitOp(0xfdab, i32x4_trunc_sat_f32x4_s   , "i32x4.trunc_sat_f32x4_s"   , NoImm                     , UNARY(v128,v128)     , simd                   )   \
	visitOp(0xfdac, i32x4_trunc_sat_f32x4_u   , "i32x4.trunc_sat_f32x4_u"   , NoImm                     , UNARY(v128,v128)     , simd                   )   \
	visitOp(0xfdad, i64x2_trunc_sat_f64x2_s   , "i64x2.trunc_sat_f64x2_s"   , NoImm                     , UNARY(v128,v128)     , simd                   )   \
	visitOp(0xfdae, i64x2_trunc_sat_f64x2_u   , "i64x2.trunc_sat_f64x2_u"   , NoImm                     , UNARY(v128,v128)     , simd                   )   \
	visitOp(0xfdaf, f32x4_convert_i32x4_s     , "f32x4.convert_i32x4_s"     , NoImm                     , UNARY(v128,v128)     , simd                   )   \
	visitOp(0xfdb0, f32x4_convert_i32x4_u     , "f32x4.convert_i32x4_u"     , NoImm                     , UNARY(v128,v128)     , simd                   )   \
	visitOp(0xfdb1, f64x2_convert_i64x2_s     , "f64x2.convert_i64x2_s"     , NoImm                     , UNARY(v128,v128)     , simd                   )   \
	visitOp(0xfdb2, f64x2_convert_i64x2_u     , "f64x2.convert_i64x2_u"     , NoImm                     , UNARY(v128,v128)     , simd                   )   \
/* v128 shuffles                                                                                                                                         */ \
	visitOp(0xfdc0, v8x16_shuffle1            , "v8x16.shuffle1"            , NoImm                     , BINARY(v128,v128)    , simd                   )   \
	visitOp(0xfdc1, v8x16_shuffle2_imm        , "v8x16.shuffle2_imm"        , ShuffleImm<16>            , BINARY(v128,v128)    , simd                   )   \
/* Atomic wait/wake                                                                                                                                      */ \
	visitOp(0xfe00, atomic_notify             , "atomic.notify"             , AtomicLoadOrStoreImm<2>   , BINARY(i32,i32)      , atomics                )   \
	visitOp(0xfe01, i32_atomic_wait           , "i32.atomic.wait"           , AtomicLoadOrStoreImm<2>   , WAIT(i32)            , atomics                )   \
	visitOp(0xfe02, i64_atomic_wait           , "i64.atomic.wait"           , AtomicLoadOrStoreImm<3>   , WAIT(i64)            , atomics                )   \
/* Atomic load/store                                                                                                                                     */ \
	visitOp(0xfe10, i32_atomic_load           , "i32.atomic.load"           , AtomicLoadOrStoreImm<2>   , LOAD(i32)            , atomics                )   \
	visitOp(0xfe11, i64_atomic_load           , "i64.atomic.load"           , AtomicLoadOrStoreImm<3>   , LOAD(i64)            , atomics                )   \
	visitOp(0xfe12, i32_atomic_load8_u        , "i32.atomic.load8_u"        , AtomicLoadOrStoreImm<0>   , LOAD(i32)            , atomics                )   \
	visitOp(0xfe13, i32_atomic_load16_u       , "i32.atomic.load16_u"       , AtomicLoadOrStoreImm<1>   , LOAD(i32)            , atomics                )   \
	visitOp(0xfe14, i64_atomic_load8_u        , "i64.atomic.load8_u"        , AtomicLoadOrStoreImm<0>   , LOAD(i64)            , atomics                )   \
	visitOp(0xfe15, i64_atomic_load16_u       , "i64.atomic.load16_u"       , AtomicLoadOrStoreImm<1>   , LOAD(i64)            , atomics                )   \
	visitOp(0xfe16, i64_atomic_load32_u       , "i64.atomic.load32_u"       , AtomicLoadOrStoreImm<2>   , LOAD(i64)            , atomics                )   \
	visitOp(0xfe17, i32_atomic_store          , "i32.atomic.store"          , AtomicLoadOrStoreImm<2>   , STORE(i32)           , atomics                )   \
	visitOp(0xfe18, i64_atomic_store          , "i64.atomic.store"          , AtomicLoadOrStoreImm<3>   , STORE(i64)           , atomics                )   \
	visitOp(0xfe19, i32_atomic_store8         , "i32.atomic.store8"         , AtomicLoadOrStoreImm<0>   , STORE(i32)           , atomics                )   \
	visitOp(0xfe1a, i32_atomic_store16        , "i32.atomic.store16"        , AtomicLoadOrStoreImm<1>   , STORE(i32)           , atomics                )   \
	visitOp(0xfe1b, i64_atomic_store8         , "i64.atomic.store8"         , AtomicLoadOrStoreImm<0>   , STORE(i64)           , atomics                )   \
	visitOp(0xfe1c, i64_atomic_store16        , "i64.atomic.store16"        , AtomicLoadOrStoreImm<1>   , STORE(i64)           , atomics                )   \
	visitOp(0xfe1d, i64_atomic_store32        , "i64.atomic.store32"        , AtomicLoadOrStoreImm<2>   , STORE(i64)           , atomics                )   \
/* Atomic read-modify-write                                                                                                                              */ \
	visitOp(0xfe1e, i32_atomic_rmw_add        , "i32.atomic.rmw.add"        , AtomicLoadOrStoreImm<2>   , ATOMICRMW(i32)       , atomics                )   \
	visitOp(0xfe1f, i64_atomic_rmw_add        , "i64.atomic.rmw.add"        , AtomicLoadOrStoreImm<3>   , ATOMICRMW(i64)       , atomics                )   \
	visitOp(0xfe20, i32_atomic_rmw8_add_u     , "i32.atomic.rmw8.add_u"     , AtomicLoadOrStoreImm<0>   , ATOMICRMW(i32)       , atomics                )   \
	visitOp(0xfe21, i32_atomic_rmw16_add_u    , "i32.atomic.rmw16.add_u"    , AtomicLoadOrStoreImm<1>   , ATOMICRMW(i32)       , atomics                )   \
	visitOp(0xfe22, i64_atomic_rmw8_add_u     , "i64.atomic.rmw8.add_u"     , AtomicLoadOrStoreImm<0>   , ATOMICRMW(i64)       , atomics                )   \
	visitOp(0xfe23, i64_atomic_rmw16_add_u    , "i64.atomic.rmw16.add_u"    , AtomicLoadOrStoreImm<1>   , ATOMICRMW(i64)       , atomics                )   \
	visitOp(0xfe24, i64_atomic_rmw32_add_u    , "i64.atomic.rmw32.add_u"    , AtomicLoadOrStoreImm<2>   , ATOMICRMW(i64)       , atomics                )   \
	visitOp(0xfe25, i32_atomic_rmw_sub        , "i32.atomic.rmw.sub"        , AtomicLoadOrStoreImm<2>   , ATOMICRMW(i32)       , atomics                )   \
	visitOp(0xfe26, i64_atomic_rmw_sub        , "i64.atomic.rmw.sub"        , AtomicLoadOrStoreImm<3>   , ATOMICRMW(i64)       , atomics                )   \
	visitOp(0xfe27, i32_atomic_rmw8_sub_u     , "i32.atomic.rmw8.sub_u"     , AtomicLoadOrStoreImm<0>   , ATOMICRMW(i32)       , atomics                )   \
	visitOp(0xfe28, i32_atomic_rmw16_sub_u    , "i32.atomic.rmw16.sub_u"    , AtomicLoadOrStoreImm<1>   , ATOMICRMW(i32)       , atomics                )   \
	visitOp(0xfe29, i64_atomic_rmw8_sub_u     , "i64.atomic.rmw8.sub_u"     , AtomicLoadOrStoreImm<0>   , ATOMICRMW(i64)       , atomics                )   \
	visitOp(0xfe2a, i64_atomic_rmw16_sub_u    , "i64.atomic.rmw16.sub_u"    , AtomicLoadOrStoreImm<1>   , ATOMICRMW(i64)       , atomics                )   \
	visitOp(0xfe2b, i64_atomic_rmw32_sub_u    , "i64.atomic.rmw32.sub_u"    , AtomicLoadOrStoreImm<2>   , ATOMICRMW(i64)       , atomics                )   \
	visitOp(0xfe2c, i32_atomic_rmw_and        , "i32.atomic.rmw.and"        , AtomicLoadOrStoreImm<2>   , ATOMICRMW(i32)       , atomics                )   \
	visitOp(0xfe2d, i64_atomic_rmw_and        , "i64.atomic.rmw.and"        , AtomicLoadOrStoreImm<3>   , ATOMICRMW(i64)       , atomics                )   \
	visitOp(0xfe2e, i32_atomic_rmw8_and_u     , "i32.atomic.rmw8.and_u"     , AtomicLoadOrStoreImm<0>   , ATOMICRMW(i32)       , atomics                )   \
	visitOp(0xfe2f, i32_atomic_rmw16_and_u    , "i32.atomic.rmw16.and_u"    , AtomicLoadOrStoreImm<1>   , ATOMICRMW(i32)       , atomics                )   \
	visitOp(0xfe30, i64_atomic_rmw8_and_u     , "i64.atomic.rmw8.and_u"     , AtomicLoadOrStoreImm<0>   , ATOMICRMW(i64)       , atomics                )   \
	visitOp(0xfe31, i64_atomic_rmw16_and_u    , "i64.atomic.rmw16.and_u"    , AtomicLoadOrStoreImm<1>   , ATOMICRMW(i64)       , atomics                )   \
	visitOp(0xfe32, i64_atomic_rmw32_and_u    , "i64.atomic.rmw32.and_u"    , AtomicLoadOrStoreImm<2>   , ATOMICRMW(i64)       , atomics                )   \
	visitOp(0xfe33, i32_atomic_rmw_or         , "i32.atomic.rmw.or"         , AtomicLoadOrStoreImm<2>   , ATOMICRMW(i32)       , atomics                )   \
	visitOp(0xfe34, i64_atomic_rmw_or         , "i64.atomic.rmw.or"         , AtomicLoadOrStoreImm<3>   , ATOMICRMW(i64)       , atomics                )   \
	visitOp(0xfe35, i32_atomic_rmw8_or_u      , "i32.atomic.rmw8.or_u"      , AtomicLoadOrStoreImm<0>   , ATOMICRMW(i32)       , atomics                )   \
	visitOp(0xfe36, i32_atomic_rmw16_or_u     , "i32.atomic.rmw16.or_u"     , AtomicLoadOrStoreImm<1>   , ATOMICRMW(i32)       , atomics                )   \
	visitOp(0xfe37, i64_atomic_rmw8_or_u      , "i64.atomic.rmw8.or_u"      , AtomicLoadOrStoreImm<0>   , ATOMICRMW(i64)       , atomics                )   \
	visitOp(0xfe38, i64_atomic_rmw16_or_u     , "i64.atomic.rmw16.or_u"     , AtomicLoadOrStoreImm<1>   , ATOMICRMW(i64)       , atomics                )   \
	visitOp(0xfe39, i64_atomic_rmw32_or_u     , "i64.atomic.rmw32.or_u"     , AtomicLoadOrStoreImm<2>   , ATOMICRMW(i64)       , atomics                )   \
	visitOp(0xfe3a, i32_atomic_rmw_xor        , "i32.atomic.rmw.xor"        , AtomicLoadOrStoreImm<2>   , ATOMICRMW(i32)       , atomics                )   \
	visitOp(0xfe3b, i64_atomic_rmw_xor        , "i64.atomic.rmw.xor"        , AtomicLoadOrStoreImm<3>   , ATOMICRMW(i64)       , atomics                )   \
	visitOp(0xfe3c, i32_atomic_rmw8_xor_u     , "i32.atomic.rmw8.xor_u"     , AtomicLoadOrStoreImm<0>   , ATOMICRMW(i32)       , atomics                )   \
	visitOp(0xfe3d, i32_atomic_rmw16_xor_u    , "i32.atomic.rmw16.xor_u"    , AtomicLoadOrStoreImm<1>   , ATOMICRMW(i32)       , atomics                )   \
	visitOp(0xfe3e, i64_atomic_rmw8_xor_u     , "i64.atomic.rmw8.xor_u"     , AtomicLoadOrStoreImm<0>   , ATOMICRMW(i64)       , atomics                )   \
	visitOp(0xfe3f, i64_atomic_rmw16_xor_u    , "i64.atomic.rmw16.xor_u"    , AtomicLoadOrStoreImm<1>   , ATOMICRMW(i64)       , atomics                )   \
	visitOp(0xfe40, i64_atomic_rmw32_xor_u    , "i64.atomic.rmw32.xor_u"    , AtomicLoadOrStoreImm<2>   , ATOMICRMW(i64)       , atomics                )   \
	visitOp(0xfe41, i32_atomic_rmw_xchg       , "i32.atomic.rmw.xchg"       , AtomicLoadOrStoreImm<2>   , ATOMICRMW(i32)       , atomics                )   \
	visitOp(0xfe42, i64_atomic_rmw_xchg       , "i64.atomic.rmw.xchg"       , AtomicLoadOrStoreImm<3>   , ATOMICRMW(i64)       , atomics                )   \
	visitOp(0xfe43, i32_atomic_rmw8_xchg_u    , "i32.atomic.rmw8.xchg_u"    , AtomicLoadOrStoreImm<0>   , ATOMICRMW(i32)       , atomics                )   \
	visitOp(0xfe44, i32_atomic_rmw16_xchg_u   , "i32.atomic.rmw16.xchg_u"   , AtomicLoadOrStoreImm<1>   , ATOMICRMW(i32)       , atomics                )   \
	visitOp(0xfe45, i64_atomic_rmw8_xchg_u    , "i64.atomic.rmw8.xchg_u"    , AtomicLoadOrStoreImm<0>   , ATOMICRMW(i64)       , atomics                )   \
	visitOp(0xfe46, i64_atomic_rmw16_xchg_u   , "i64.atomic.rmw16.xchg_u"   , AtomicLoadOrStoreImm<1>   , ATOMICRMW(i64)       , atomics                )   \
	visitOp(0xfe47, i64_atomic_rmw32_xchg_u   , "i64.atomic.rmw32.xchg_u"   , AtomicLoadOrStoreImm<2>   , ATOMICRMW(i64)       , atomics                )   \
	visitOp(0xfe48, i32_atomic_rmw_cmpxchg    , "i32.atomic.rmw.cmpxchg"    , AtomicLoadOrStoreImm<2>   , COMPAREEXCHANGE(i32) , atomics                )   \
	visitOp(0xfe49, i64_atomic_rmw_cmpxchg    , "i64.atomic.rmw.cmpxchg"    , AtomicLoadOrStoreImm<3>   , COMPAREEXCHANGE(i64) , atomics                )   \
	visitOp(0xfe4a, i32_atomic_rmw8_cmpxchg_u , "i32.atomic.rmw8.cmpxchg_u" , AtomicLoadOrStoreImm<0>   , COMPAREEXCHANGE(i32) , atomics                )   \
	visitOp(0xfe4b, i32_atomic_rmw16_cmpxchg_u, "i32.atomic.rmw16.cmpxchg_u", AtomicLoadOrStoreImm<1>   , COMPAREEXCHANGE(i32) , atomics                )   \
	visitOp(0xfe4c, i64_atomic_rmw8_cmpxchg_u , "i64.atomic.rmw8.cmpxchg_u" , AtomicLoadOrStoreImm<0>   , COMPAREEXCHANGE(i64) , atomics                )   \
	visitOp(0xfe4d, i64_atomic_rmw16_cmpxchg_u, "i64.atomic.rmw16.cmpxchg_u", AtomicLoadOrStoreImm<1>   , COMPAREEXCHANGE(i64) , atomics                )   \
	visitOp(0xfe4e, i64_atomic_rmw32_cmpxchg_u, "i64.atomic.rmw32.cmpxchg_u", AtomicLoadOrStoreImm<2>   , COMPAREEXCHANGE(i64) , atomics                )

// clang-format on

#define ENUM_NONCONTROL_OPERATORS(visitOp)                                                         \
	ENUM_PARAMETRIC_OPERATORS(visitOp)                                                             \
	ENUM_NONCONTROL_NONPARAMETRIC_OPERATORS(visitOp)

#define ENUM_OPERATORS(visitOp)                                                                    \
	ENUM_NONCONTROL_OPERATORS(visitOp)                                                             \
	ENUM_CONTROL_OPERATORS(visitOp)
