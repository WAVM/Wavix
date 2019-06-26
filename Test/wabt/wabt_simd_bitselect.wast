;; Tests from wabt: https://github.com/WebAssembly/wabt/tree/master/test/interp
;; Distributed under the terms of the wabt license: https://github.com/WebAssembly/wabt/blob/master/LICENSE
;; Modified for compatibility with WAVM's interpretation of the proposed spec.

(module
  ;; v128.bitselect
  (func (export  "func_v128_bitselect_0") (result  v128)
    v128.const i32x4 0x00ff0001 0x00040002 0x55555555 0x00000004
    v128.const i32x4 0x00020001 0x00fe0002 0xaaaaaaaa 0x55000004
    v128.const i32x4 0xffffffff 0x00000000 0x55555555 0x55000004
    v128.bitselect)
)

(assert_return (invoke "func_v128_bitselect_0") (v128.const i32x4 0x00ff0001 0x00fe0002 0xffffffff 0x00000004))
