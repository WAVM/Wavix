Primary repo: https://github.com/WAVM/Wavix

[License](LICENSE.md)

# Overview

Wavix is a POSIX system that uses WebAssembly's Software Fault Isolation to run the complete system in a single host OS process.

# Components

* [WAVM](https://github.com/WAVM/WAVM) is the sister project of Wavix, and provides a browser-independent WebAssembly runtime.
* Clang is a C/C++ compiler that targets WebAssembly. It depends on LLVM as its backend, and compiler-rt as a runtime library for various features.
* LLD is the LLVM project's linker that supports linking the WebAssembly object files produced by LLVM's WebAssembly backend.
* musl is a C standard library implementation for Linux. Currently, the Wavix "kernel" in WAVM emulates the Linux syscalls made by musl.
* libcxx and libcxxabi are the LLVM project's implementation of the C++ standard library.
* bash and coreutils are the GNU project's famous shell, and a core set of tools that complement it (think `ls`, `cp`, `chmod`, etc).

# Bootstrapping

Bootstrapping Wavix will build the compiler (Clang, LLVM, and LLD) with your host compiler, then compile the runtime libraries (compiler-rt, musl, libcxx+libcxxabi), bash, and coreutils with the resulting compiler.

To bootstrap Wavix, I use Ubuntu 18.04 running on the Windows Subsystem for Linux. I originally used Ubuntu 16.04, and AFAIK that will still work.

* You need these packages (and maybe more): `sudo apt install build-essential gcc g++ ninja-build python`

* Clone this repository. You can clone it wherever you want, but for purposes of these instructions, I'll refer to the directory containing the source as `~/Wavix`.

* To bootstrap, make a directory somewhere for the build. You can call it whatever you want, but for purposes of these instructions, I'll call it `~/wavix-build`.

* From the `~/wavix-build` directory, run `python ~/Wavix/bootstrap/build.py`. It will probably take a while to complete.

* Upon successful completion, the `~/wavix-build/bootstrap` directory will contain the bootstrap toolchain. The `~/wavix-build/sys` directory will contain the bootstrapped Wavix system: Wavix binaries for bash+coreutils, the Wavix libraries for compiler-rt, libc, and libc++, and the headers for the same. To compile a C++ file to a Wavix binary, you can run `~/wavix-build/bootstrap/bin/clang++ -target wasm32-unknown-wavix --sysroot ~/wavix-build/sys a.cpp`.

The bootstrap process will also work on Windows (without the Windows Subsystem for Linux) to compile the compiler, runtime libraries, and WAVM, but not to compile bash+coreutils.

# Running Wavix

To run a Wavix binary, use the command `~/wavix-build/bootstrap/bin/wavix --sysroot ~/wavix-build/sys <binary>`.

For example: `~/wavix-build/bootstrap/bin/wavix --sysroot ~/wavix-build/sys ~/wavix-build/sys/bin/bash`

# Status

The toolchain is mostly functional, thanks to the effort of the WebAssembly group on LLVM/LLD.

The Wavix runtime implements enough syscalls for bash to start, but not enough to do anything interesting with it.

# Subtrees

This repository integrates many other repos as subdirectories using the [git subtree](https://git-scm.com/book/en/v1/Git-Tools-Subtree-Merging) feature. This allows making atomics commits that modify our version of all repos, but still tracks the relationship between those subdirectories and their upstream repos. To pull changes from the upstream repos, use these commands:

* `git subtree pull --squash --prefix clang https://llvm.org/git/clang.git master`
* `git subtree pull --squash --prefix compiler-rt https://llvm.org/git/compiler-rt.git master`
* `git subtree pull --squash --prefix libcxx https://llvm.org/git/libcxx.git master`
* `git subtree pull --squash --prefix libcxxabi https://llvm.org/git/libcxxabi.git master`
* `git subtree pull --squash --prefix lld https://llvm.org/git/lld.git master`
* `git subtree pull --squash --prefix llvm https://llvm.org/git/llvm.git master`
* `git subtree pull --squash --prefix musl https://github.com/jfbastien/musl wasm-prototype-1`
* `git subtree pull --squash --prefix WAVM https://github.com/WAVM/WAVM master`