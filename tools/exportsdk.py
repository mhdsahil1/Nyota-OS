#!/usr/bin/env python3
"""
Nyota OS — Developer SDK Exporter (exportsdk.py)
Prepares and populates the standalone developer SDK in sdk/.
"""

import os
import shutil

ROOT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SDK_DIR = os.path.join(ROOT_DIR, "sdk")
BUILD_DIR = os.path.join(ROOT_DIR, "build")

def main():
    inc_dir = os.path.join(SDK_DIR, "include")
    lib_dir = os.path.join(SDK_DIR, "lib")

    os.makedirs(inc_dir, exist_ok=True)
    os.makedirs(lib_dir, exist_ok=True)

    files_to_copy = [
        (os.path.join(ROOT_DIR, "user", "libnyota", "libnyota.h"), os.path.join(inc_dir, "libnyota.h")),
        (os.path.join(ROOT_DIR, "include", "types.h"), os.path.join(inc_dir, "types.h")),
        (os.path.join(BUILD_DIR, "crt0.o"), os.path.join(lib_dir, "crt0.o")),
        (os.path.join(BUILD_DIR, "libnyota.a"), os.path.join(lib_dir, "libnyota.a")),
        (os.path.join(ROOT_DIR, "user.ld"), os.path.join(SDK_DIR, "user.ld")),
    ]

    for src, dst in files_to_copy:
        if os.path.isfile(src):
            shutil.copyfile(src, dst)
            print(f"[SDK] Copied {os.path.relpath(src, ROOT_DIR)} -> {os.path.relpath(dst, ROOT_DIR)}")
        else:
            print(f"[SDK] Warning: source file not found: {os.path.relpath(src, ROOT_DIR)}")

    print(f"[SDK] Nyota OS Developer Platform SDK exported to {os.path.relpath(SDK_DIR, ROOT_DIR)}/")

if __name__ == "__main__":
    main()
