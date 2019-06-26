;; Tests from wabt: https://github.com/WebAssembly/wabt/tree/master/test/interp
;; Distributed under the terms of the wabt license: https://github.com/WebAssembly/wabt/blob/master/LICENSE
;; Modified for compatibility with WAVM's interpretation of the proposed spec.

(module
  (memory 1)
  (data (i32.const 0) "\ff\ff\ff\ff")
  (data (i32.const 4) "\00\00\ce\41")
  (data (i32.const 8) "\00\00\00\00\00\ff\8f\40")
  (data (i32.const 16) "\ff\ff\ff\ff\ff\ff\ff\ff")

  ;; v128 load
  (func (export "v128_load_0") (result v128)
    i32.const 4
    v128.load)

  ;; v128 store
  (func (export "v128_store_0") (result v128)
    i32.const 4
    v128.const i32x4 0x11223344 0x55667788 0x99aabbcc 0xddeeff00
    v128.store
    i32.const 4
    v128.load)
)

(assert_return (invoke "v128_load_0") (v128.const i32x4 0x41ce0000 0x00000000 0x408fff00 0xffffffff))
(assert_return (invoke "v128_store_0") (v128.const i32x4 0x11223344 0x55667788 0x99aabbcc 0xddeeff00))