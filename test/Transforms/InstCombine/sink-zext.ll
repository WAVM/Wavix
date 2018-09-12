; NOTE: Assertions have been autogenerated by utils/update_test_checks.py
; RUN: opt < %s -instcombine -S | FileCheck %s

target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

declare i32 @callee()

define i64 @test1(i32 %V) {
; CHECK-LABEL: @test1(
; CHECK-NEXT:    [[CALL1:%.*]] = call i32 @callee(), !range !0
; CHECK-NEXT:    [[CALL2:%.*]] = call i32 @callee(), !range !0
; CHECK-NEXT:    [[ADDCONV:%.*]] = add nuw nsw i32 [[CALL1]], [[CALL2]]
; CHECK-NEXT:    [[ADD:%.*]] = zext i32 [[ADD:%.*]]conv to i64
; CHECK-NEXT:    ret i64 [[ADD]]
;
  %call1 = call i32 @callee(), !range !0
  %call2 = call i32 @callee(), !range !0
  %zext1 = sext i32 %call1 to i64
  %zext2 = sext i32 %call2 to i64
  %add = add i64 %zext1, %zext2
  ret i64 %add
}

define i64 @test2(i32 %V) {
; CHECK-LABEL: @test2(
; CHECK-NEXT:    [[CALL1:%.*]] = call i32 @callee(), !range !0
; CHECK-NEXT:    [[CALL2:%.*]] = call i32 @callee(), !range !0
; CHECK-NEXT:    [[ADD:%.*]] = add nuw nsw i32 [[CALL1]], [[CALL2]]
; CHECK-NEXT:    [[ZEXT1:%.*]] = zext i32 [[ADD]] to i64
; CHECK-NEXT:    ret i64 [[ZEXT1]]
;
  %call1 = call i32 @callee(), !range !0
  %call2 = call i32 @callee(), !range !0
  %add = add i32 %call1, %call2
  %zext = sext i32 %add to i64
  ret i64 %zext
}

define i64 @test3(i32 %V) {
; CHECK-LABEL: @test3(
; CHECK-NEXT:    [[CALL1:%.*]] = call i32 @callee(), !range !0
; CHECK-NEXT:    [[CALL2:%.*]] = call i32 @callee(), !range !0
; CHECK-NEXT:    [[MULCONV:%.*]] = mul nuw nsw i32 [[CALL1]], [[CALL2]]
; CHECK-NEXT:    [[ADD:%.*]] = zext i32 [[MULCONV]] to i64
; CHECK-NEXT:    ret i64 [[ADD]]
;
  %call1 = call i32 @callee(), !range !0
  %call2 = call i32 @callee(), !range !0
  %zext1 = sext i32 %call1 to i64
  %zext2 = sext i32 %call2 to i64
  %add = mul i64 %zext1, %zext2
  ret i64 %add
}

define i64 @test4(i32 %V) {
; CHECK-LABEL: @test4(
; CHECK-NEXT:    [[CALL1:%.*]] = call i32 @callee(), !range !0
; CHECK-NEXT:    [[CALL2:%.*]] = call i32 @callee(), !range !0
; CHECK-NEXT:    [[ADD:%.*]] = mul nuw nsw i32 [[CALL1]], [[CALL2]]
; CHECK-NEXT:    [[ZEXT1:%.*]] = zext i32 [[ADD]] to i64
; CHECK-NEXT:    ret i64 [[ZEXT1]]
;
  %call1 = call i32 @callee(), !range !0
  %call2 = call i32 @callee(), !range !0
  %add = mul i32 %call1, %call2
  %zext = sext i32 %add to i64
  ret i64 %zext
}

define i64 @test5(i32 %V) {
; CHECK-LABEL: @test5(
; CHECK-NEXT:    [[ASHR:%.*]] = ashr i32 [[V:%.*]], 1
; CHECK-NEXT:    [[ADDCONV:%.*]] = add nsw i32 [[ASHR]], 1073741823
; CHECK-NEXT:    [[ADD:%.*]] = sext i32 [[ADDCONV]] to i64
; CHECK-NEXT:    ret i64 [[ADD]]
;
  %ashr = ashr i32 %V, 1
  %sext = sext i32 %ashr to i64
  %add = add i64 %sext, 1073741823
  ret i64 %add
}

define <2 x i64> @test5_splat(<2 x i32> %V) {
; CHECK-LABEL: @test5_splat(
; CHECK-NEXT:    [[ASHR:%.*]] = ashr <2 x i32> [[V:%.*]], <i32 1, i32 1>
; CHECK-NEXT:    [[ADDCONV:%.*]] = add nsw <2 x i32> [[ASHR]], <i32 1073741823, i32 1073741823>
; CHECK-NEXT:    [[ADD:%.*]] = sext <2 x i32> [[ADDCONV]] to <2 x i64>
; CHECK-NEXT:    ret <2 x i64> [[ADD]]
;
  %ashr = ashr <2 x i32> %V, <i32 1, i32 1>
  %sext = sext <2 x i32> %ashr to <2 x i64>
  %add = add <2 x i64> %sext, <i64 1073741823, i64 1073741823>
  ret <2 x i64> %add
}

define <2 x i64> @test5_vec(<2 x i32> %V) {
; CHECK-LABEL: @test5_vec(
; CHECK-NEXT:    [[ASHR:%.*]] = ashr <2 x i32> [[V:%.*]], <i32 1, i32 1>
; CHECK-NEXT:    [[ADDCONV:%.*]] = add nsw <2 x i32> [[ASHR]], <i32 1, i32 2>
; CHECK-NEXT:    [[ADD:%.*]] = sext <2 x i32> [[ADDCONV]] to <2 x i64>
; CHECK-NEXT:    ret <2 x i64> [[ADD]]
;
  %ashr = ashr <2 x i32> %V, <i32 1, i32 1>
  %sext = sext <2 x i32> %ashr to <2 x i64>
  %add = add <2 x i64> %sext, <i64 1, i64 2>
  ret <2 x i64> %add
}

define i64 @test6(i32 %V) {
; CHECK-LABEL: @test6(
; CHECK-NEXT:    [[ASHR:%.*]] = ashr i32 [[V:%.*]], 1
; CHECK-NEXT:    [[ADDCONV:%.*]] = add nsw i32 [[ASHR]], -1073741824
; CHECK-NEXT:    [[ADD:%.*]] = sext i32 [[ADDCONV]] to i64
; CHECK-NEXT:    ret i64 [[ADD]]
;
  %ashr = ashr i32 %V, 1
  %sext = sext i32 %ashr to i64
  %add = add i64 %sext, -1073741824
  ret i64 %add
}

define <2 x i64> @test6_splat(<2 x i32> %V) {
; CHECK-LABEL: @test6_splat(
; CHECK-NEXT:    [[ASHR:%.*]] = ashr <2 x i32> [[V:%.*]], <i32 1, i32 1>
; CHECK-NEXT:    [[ADDCONV:%.*]] = add nsw <2 x i32> [[ASHR]], <i32 -1073741824, i32 -1073741824>
; CHECK-NEXT:    [[ADD:%.*]] = sext <2 x i32> [[ADDCONV]] to <2 x i64>
; CHECK-NEXT:    ret <2 x i64> [[ADD]]
;
  %ashr = ashr <2 x i32> %V, <i32 1, i32 1>
  %sext = sext <2 x i32> %ashr to <2 x i64>
  %add = add <2 x i64> %sext, <i64 -1073741824, i64 -1073741824>
  ret <2 x i64> %add
}

define <2 x i64> @test6_vec(<2 x i32> %V) {
; CHECK-LABEL: @test6_vec(
; CHECK-NEXT:    [[ASHR:%.*]] = ashr <2 x i32> [[V:%.*]], <i32 1, i32 1>
; CHECK-NEXT:    [[ADDCONV:%.*]] = add nsw <2 x i32> [[ASHR]], <i32 -1, i32 -2>
; CHECK-NEXT:    [[ADD:%.*]] = sext <2 x i32> [[ADDCONV]] to <2 x i64>
; CHECK-NEXT:    ret <2 x i64> [[ADD]]
;
  %ashr = ashr <2 x i32> %V, <i32 1, i32 1>
  %sext = sext <2 x i32> %ashr to <2 x i64>
  %add = add <2 x i64> %sext, <i64 -1, i64 -2>
  ret <2 x i64> %add
}

define <2 x i64> @test6_vec2(<2 x i32> %V) {
; CHECK-LABEL: @test6_vec2(
; CHECK-NEXT:    [[ASHR:%.*]] = ashr <2 x i32> [[V:%.*]], <i32 1, i32 1>
; CHECK-NEXT:    [[ADDCONV:%.*]] = add nsw <2 x i32> [[ASHR]], <i32 -1, i32 1>
; CHECK-NEXT:    [[ADD:%.*]] = sext <2 x i32> [[ADDCONV]] to <2 x i64>
; CHECK-NEXT:    ret <2 x i64> [[ADD]]
;
  %ashr = ashr <2 x i32> %V, <i32 1, i32 1>
  %sext = sext <2 x i32> %ashr to <2 x i64>
  %add = add <2 x i64> %sext, <i64 -1, i64 1>
  ret <2 x i64> %add
}

define i64 @test7(i32 %V) {
; CHECK-LABEL: @test7(
; CHECK-NEXT:    [[LSHR:%.*]] = lshr i32 [[V:%.*]], 1
; CHECK-NEXT:    [[ADDCONV:%.*]] = add nuw i32 [[LSHR]], 2147483647
; CHECK-NEXT:    [[ADD:%.*]] = zext i32 [[ADDCONV]] to i64
; CHECK-NEXT:    ret i64 [[ADD]]
;
  %lshr = lshr i32 %V, 1
  %zext = zext i32 %lshr to i64
  %add = add i64 %zext, 2147483647
  ret i64 %add
}

define <2 x i64> @test7_splat(<2 x i32> %V) {
; CHECK-LABEL: @test7_splat(
; CHECK-NEXT:    [[LSHR:%.*]] = lshr <2 x i32> [[V:%.*]], <i32 1, i32 1>
; CHECK-NEXT:    [[ADDCONV:%.*]] = add nuw <2 x i32> [[LSHR]], <i32 2147483647, i32 2147483647>
; CHECK-NEXT:    [[ADD:%.*]] = zext <2 x i32> [[ADDCONV]] to <2 x i64>
; CHECK-NEXT:    ret <2 x i64> [[ADD]]
;
  %lshr = lshr <2 x i32> %V, <i32 1, i32 1>
  %zext = zext <2 x i32> %lshr to <2 x i64>
  %add = add <2 x i64> %zext, <i64 2147483647, i64 2147483647>
  ret <2 x i64> %add
}

define <2 x i64> @test7_vec(<2 x i32> %V) {
; CHECK-LABEL: @test7_vec(
; CHECK-NEXT:    [[LSHR:%.*]] = lshr <2 x i32> [[V:%.*]], <i32 1, i32 1>
; CHECK-NEXT:    [[ADDCONV:%.*]] = add nuw <2 x i32> [[LSHR]], <i32 1, i32 2>
; CHECK-NEXT:    [[ADD:%.*]] = zext <2 x i32> [[ADDCONV]] to <2 x i64>
; CHECK-NEXT:    ret <2 x i64> [[ADD]]
;
  %lshr = lshr <2 x i32> %V, <i32 1, i32 1>
  %zext = zext <2 x i32> %lshr to <2 x i64>
  %add = add <2 x i64> %zext, <i64 1, i64 2>
  ret <2 x i64> %add
}

define i64 @test8(i32 %V) {
; CHECK-LABEL: @test8(
; CHECK-NEXT:    [[ASHR:%.*]] = ashr i32 [[V:%.*]], 16
; CHECK-NEXT:    [[MULCONV:%.*]] = mul nsw i32 [[ASHR]], 32767
; CHECK-NEXT:    [[MUL:%.*]] = sext i32 [[MULCONV]] to i64
; CHECK-NEXT:    ret i64 [[MUL]]
;
  %ashr = ashr i32 %V, 16
  %sext = sext i32 %ashr to i64
  %mul = mul i64 %sext, 32767
  ret i64 %mul
}

define <2 x i64> @test8_splat(<2 x i32> %V) {
; CHECK-LABEL: @test8_splat(
; CHECK-NEXT:    [[ASHR:%.*]] = ashr <2 x i32> [[V:%.*]], <i32 16, i32 16>
; CHECK-NEXT:    [[MULCONV:%.*]] = mul nsw <2 x i32> [[ASHR]], <i32 32767, i32 32767>
; CHECK-NEXT:    [[MUL:%.*]] = sext <2 x i32> [[MULCONV]] to <2 x i64>
; CHECK-NEXT:    ret <2 x i64> [[MUL]]
;
  %ashr = ashr <2 x i32> %V, <i32 16, i32 16>
  %sext = sext <2 x i32> %ashr to <2 x i64>
  %mul = mul <2 x i64> %sext, <i64 32767, i64 32767>
  ret <2 x i64> %mul
}

define <2 x i64> @test8_vec(<2 x i32> %V) {
; CHECK-LABEL: @test8_vec(
; CHECK-NEXT:    [[ASHR:%.*]] = ashr <2 x i32> [[V:%.*]], <i32 16, i32 16>
; CHECK-NEXT:    [[MULCONV:%.*]] = mul nsw <2 x i32> [[ASHR]], <i32 32767, i32 16384>
; CHECK-NEXT:    [[MUL:%.*]] = sext <2 x i32> [[MULCONV]] to <2 x i64>
; CHECK-NEXT:    ret <2 x i64> [[MUL]]
;
  %ashr = ashr <2 x i32> %V, <i32 16, i32 16>
  %sext = sext <2 x i32> %ashr to <2 x i64>
  %mul = mul <2 x i64> %sext, <i64 32767, i64 16384>
  ret <2 x i64> %mul
}

define <2 x i64> @test8_vec2(<2 x i32> %V) {
; CHECK-LABEL: @test8_vec2(
; CHECK-NEXT:    [[ASHR:%.*]] = ashr <2 x i32> [[V:%.*]], <i32 16, i32 16>
; CHECK-NEXT:    [[MULCONV:%.*]] = mul nsw <2 x i32> [[ASHR]], <i32 32767, i32 -32767>
; CHECK-NEXT:    [[MUL:%.*]] = sext <2 x i32> [[MULCONV]] to <2 x i64>
; CHECK-NEXT:    ret <2 x i64> [[MUL]]
;
  %ashr = ashr <2 x i32> %V, <i32 16, i32 16>
  %sext = sext <2 x i32> %ashr to <2 x i64>
  %mul = mul <2 x i64> %sext, <i64 32767, i64 -32767>
  ret <2 x i64> %mul
}

define i64 @test9(i32 %V) {
; CHECK-LABEL: @test9(
; CHECK-NEXT:    [[ASHR:%.*]] = ashr i32 [[V:%.*]], 16
; CHECK-NEXT:    [[MULCONV:%.*]] = mul nsw i32 [[ASHR]], -32767
; CHECK-NEXT:    [[MUL:%.*]] = sext i32 [[MULCONV]] to i64
; CHECK-NEXT:    ret i64 [[MUL]]
;
  %ashr = ashr i32 %V, 16
  %sext = sext i32 %ashr to i64
  %mul = mul i64 %sext, -32767
  ret i64 %mul
}

define <2 x i64> @test9_splat(<2 x i32> %V) {
; CHECK-LABEL: @test9_splat(
; CHECK-NEXT:    [[ASHR:%.*]] = ashr <2 x i32> [[V:%.*]], <i32 16, i32 16>
; CHECK-NEXT:    [[MULCONV:%.*]] = mul nsw <2 x i32> [[ASHR]], <i32 -32767, i32 -32767>
; CHECK-NEXT:    [[MUL:%.*]] = sext <2 x i32> [[MULCONV]] to <2 x i64>
; CHECK-NEXT:    ret <2 x i64> [[MUL]]
;
  %ashr = ashr <2 x i32> %V, <i32 16, i32 16>
  %sext = sext <2 x i32> %ashr to <2 x i64>
  %mul = mul <2 x i64> %sext, <i64 -32767, i64 -32767>
  ret <2 x i64> %mul
}

define <2 x i64> @test9_vec(<2 x i32> %V) {
; CHECK-LABEL: @test9_vec(
; CHECK-NEXT:    [[ASHR:%.*]] = ashr <2 x i32> [[V:%.*]], <i32 16, i32 16>
; CHECK-NEXT:    [[MULCONV:%.*]] = mul nsw <2 x i32> [[ASHR]], <i32 -32767, i32 -10>
; CHECK-NEXT:    [[MUL:%.*]] = sext <2 x i32> [[MULCONV]] to <2 x i64>
; CHECK-NEXT:    ret <2 x i64> [[MUL]]
;
  %ashr = ashr <2 x i32> %V, <i32 16, i32 16>
  %sext = sext <2 x i32> %ashr to <2 x i64>
  %mul = mul <2 x i64> %sext, <i64 -32767, i64 -10>
  ret <2 x i64> %mul
}

define i64 @test10(i32 %V) {
; CHECK-LABEL: @test10(
; CHECK-NEXT:    [[LSHR:%.*]] = lshr i32 [[V:%.*]], 16
; CHECK-NEXT:    [[MULCONV:%.*]] = mul nuw i32 [[LSHR]], 65535
; CHECK-NEXT:    [[MUL:%.*]] = zext i32 [[MULCONV]] to i64
; CHECK-NEXT:    ret i64 [[MUL]]
;
  %lshr = lshr i32 %V, 16
  %zext = zext i32 %lshr to i64
  %mul = mul i64 %zext, 65535
  ret i64 %mul
}

define <2 x i64> @test10_splat(<2 x i32> %V) {
; CHECK-LABEL: @test10_splat(
; CHECK-NEXT:    [[LSHR:%.*]] = lshr <2 x i32> [[V:%.*]], <i32 16, i32 16>
; CHECK-NEXT:    [[MULCONV:%.*]] = mul nuw <2 x i32> [[LSHR]], <i32 65535, i32 65535>
; CHECK-NEXT:    [[MUL:%.*]] = zext <2 x i32> [[MULCONV]] to <2 x i64>
; CHECK-NEXT:    ret <2 x i64> [[MUL]]
;
  %lshr = lshr <2 x i32> %V, <i32 16, i32 16>
  %zext = zext <2 x i32> %lshr to <2 x i64>
  %mul = mul <2 x i64> %zext, <i64 65535, i64 65535>
  ret <2 x i64> %mul
}

define <2 x i64> @test10_vec(<2 x i32> %V) {
; CHECK-LABEL: @test10_vec(
; CHECK-NEXT:    [[LSHR:%.*]] = lshr <2 x i32> [[V:%.*]], <i32 16, i32 16>
; CHECK-NEXT:    [[MULCONV:%.*]] = mul nuw <2 x i32> [[LSHR]], <i32 65535, i32 2>
; CHECK-NEXT:    [[MUL:%.*]] = zext <2 x i32> [[MULCONV]] to <2 x i64>
; CHECK-NEXT:    ret <2 x i64> [[MUL]]
;
  %lshr = lshr <2 x i32> %V, <i32 16, i32 16>
  %zext = zext <2 x i32> %lshr to <2 x i64>
  %mul = mul <2 x i64> %zext, <i64 65535, i64 2>
  ret <2 x i64> %mul
}

!0 = !{ i32 0, i32 2000 }
