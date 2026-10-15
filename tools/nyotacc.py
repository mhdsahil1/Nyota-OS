#!/usr/bin/env python3
"""
Nyota OS — Native Application Cross-Compiler Driver (nyotacc)
Compiles C source code into native 64-bit static ELF executables for Nyota OS.

Usage:
  nyotacc [-o output] [CFLAGS] file1.c [file2.c ...] [LDFLAGS]

Example:
  python tools/nyotacc.py -o hello hello.c
"""

import sys
import os
import subprocess
import tempfile
import shutil

ROOT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Auto-detect tools
CC = os.environ.get("CC", "gcc")
LD = os.environ.get("LD", "ld")
OBJCOPY = os.environ.get("OBJCOPY", "objcopy")

# SDK paths
SDK_DIR = os.path.join(ROOT_DIR, "sdk")
BUILD_DIR = os.path.join(ROOT_DIR, "build")

# Headers
INC_PATHS = [
    os.path.join(SDK_DIR, "include"),
    os.path.join(ROOT_DIR, "user", "libnyota"),
    os.path.join(ROOT_DIR, "include"),
]

# Libraries & linker script
CRT0_PATH = os.path.join(SDK_DIR, "lib", "crt0.o")
if not os.path.isfile(CRT0_PATH):
    CRT0_PATH = os.path.join(BUILD_DIR, "crt0.o")

LIBNYOTA_PATH = os.path.join(SDK_DIR, "lib", "libnyota.a")
if not os.path.isfile(LIBNYOTA_PATH):
    LIBNYOTA_PATH = os.path.join(BUILD_DIR, "libnyota.a")

USER_LD_PATH = os.path.join(SDK_DIR, "user.ld")
if not os.path.isfile(USER_LD_PATH):
    USER_LD_PATH = os.path.join(ROOT_DIR, "user.ld")

DEFAULT_CFLAGS = [
    "-m64",
    "-ffreestanding",
    "-fno-stack-protector",
    "-fno-pie",
    "-fno-pic",
    "-mno-red-zone",
    "-Wall",
    "-Wextra",
]

DEFAULT_LDFLAGS = [
    "-nostdlib",
    "-m", "i386pep",
    "--image-base", "0x0000008000000000",
]

def print_help():
    print("Nyota OS Native C Compiler Driver (nyotacc) v1.5.0")
    print("Usage: nyotacc [options] file...")
    print("Options:")
    print("  -o <file>      Place the executable into <file> (default: a.out)")
    print("  -c             Compile to object file only, do not link")
    print("  -v             Verbose diagnostic output")
    print("  -I<dir>        Add include directory")
    print("  -D<macro>      Define preprocessor macro")
    print("  --help         Display this help message")

def main():
    args = sys.argv[1:]
    if not args or "--help" in args or "-h" in args:
        print_help()
        sys.exit(0)

    output_file = "a.out"
    compile_only = False
    verbose = False
    extra_cflags = []
    source_files = []
    object_files = []

    i = 0
    while i < len(args):
        arg = args[i]
        if arg == "-o":
            i += 1
            if i < len(args):
                output_file = args[i]
        elif arg == "-c":
            compile_only = True
        elif arg == "-v":
            verbose = True
        elif arg.startswith("-I") or arg.startswith("-D") or arg.startswith("-O") or arg.startswith("-g"):
            extra_cflags.append(arg)
        elif arg.endswith(".c"):
            source_files.append(arg)
        elif arg.endswith(".o") or arg.endswith(".a"):
            object_files.append(arg)
        else:
            extra_cflags.append(arg)
        i += 1

    if not source_files and not object_files:
        print("nyotacc: fatal error: no input files", file=sys.stderr)
        sys.exit(1)

    # Collect include flags
    inc_flags = []
    for inc in INC_PATHS:
        if os.path.isdir(inc):
            inc_flags.extend(["-I", inc])

    tmp_dir = tempfile.mkdtemp(prefix="nyotacc_")
    try:
        compiled_objs = []
        for src in source_files:
            base = os.path.splitext(os.path.basename(src))[0]
            obj_path = os.path.join(tmp_dir, f"{base}.o")
            cmd = [CC] + DEFAULT_CFLAGS + inc_flags + extra_cflags + ["-c", src, "-o", obj_path]
            if verbose:
                print("[CC]  " + " ".join(cmd))
            ret = subprocess.run(cmd)
            if ret.returncode != 0:
                sys.exit(ret.returncode)
            compiled_objs.append(obj_path)

        if compile_only:
            # Just move the first object file or objects
            if len(compiled_objs) == 1:
                target = output_file if output_file != "a.out" else os.path.splitext(source_files[0])[0] + ".o"
                shutil.copyfile(compiled_objs[0], target)
            else:
                for obj in compiled_objs:
                    shutil.copy(obj, ".")
            return

        # Linking
        all_objs = compiled_objs + object_files
        intermediate_pe = os.path.join(tmp_dir, "intermediate.pe")

        if not os.path.isfile(CRT0_PATH):
            print(f"nyotacc: error: crt0.o not found at '{CRT0_PATH}'. Run 'make build/crt0.o' first.", file=sys.stderr)
            sys.exit(1)

        if not os.path.isfile(LIBNYOTA_PATH):
            print(f"nyotacc: error: libnyota.a not found at '{LIBNYOTA_PATH}'. Run 'make user' first.", file=sys.stderr)
            sys.exit(1)

        if not os.path.isfile(USER_LD_PATH):
            print(f"nyotacc: error: user.ld linker script not found at '{USER_LD_PATH}'.", file=sys.stderr)
            sys.exit(1)

        link_cmd = [LD] + DEFAULT_LDFLAGS + ["-T", USER_LD_PATH, "-o", intermediate_pe, CRT0_PATH] + all_objs + [LIBNYOTA_PATH]
        if verbose:
            print("[LD]  " + " ".join(link_cmd))
        ret = subprocess.run(link_cmd)
        if ret.returncode != 0:
            sys.exit(ret.returncode)

        # Convert to ELF64
        objcopy_cmd = [OBJCOPY, "-O", "elf64-x86-64", intermediate_pe, output_file]
        if verbose:
            print("[ELF] " + " ".join(objcopy_cmd))
        ret = subprocess.run(objcopy_cmd)
        if ret.returncode != 0:
            sys.exit(ret.returncode)

        if verbose:
            print(f"[OK]  Successfully built Nyota OS native ELF executable: {output_file}")

    finally:
        shutil.rmtree(tmp_dir, ignore_errors=True)

if __name__ == "__main__":
    main()
