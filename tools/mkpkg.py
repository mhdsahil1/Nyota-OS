#!/usr/bin/env python3
"""
Nyota OS — Host Package Archive Builder (mkpkg.py)
Creates and inspects .npk (Nyota Package) archives for Nyota OS v1.5.0.

Usage:
  python tools/mkpkg.py create <output.npk> --name <name> --version <ver> \
      --desc <description> --author <author> \
      --file <pkg_path>=<host_file> [--file ...]

  python tools/mkpkg.py info <package.npk>
"""

import sys
import os
import struct
import argparse

NPK_MAGIC = b"NPK1"
NPK_NAME_MAX = 32
NPK_VER_MAX = 16
NPK_DESC_MAX = 64
NPK_AUTH_MAX = 32
NPK_PATH_MAX = 64

def pack_str(s: str, max_len: int) -> bytes:
    encoded = s.encode('utf-8')[:max_len - 1]
    return encoded.ljust(max_len, b'\x00')

def unpack_str(b: bytes) -> str:
    return b.split(b'\x00', 1)[0].decode('utf-8', errors='replace')

def create_package(output_path: str, name: str, version: str, desc: str, author: str, files: list):
    if not name or len(name) >= NPK_NAME_MAX:
        raise ValueError(f"Package name '{name}' too long (max {NPK_NAME_MAX - 1} chars)")
    if not version or len(version) >= NPK_VER_MAX:
        raise ValueError(f"Version '{version}' too long (max {NPK_VER_MAX - 1} chars)")
    if len(desc) >= NPK_DESC_MAX:
        raise ValueError(f"Description too long (max {NPK_DESC_MAX - 1} chars)")
    if len(author) >= NPK_AUTH_MAX:
        raise ValueError(f"Author too long (max {NPK_AUTH_MAX - 1} chars)")

    file_entries = []
    for item in files:
        if "=" in item:
            pkg_path, host_path = item.split("=", 1)
        else:
            host_path = item
            pkg_path = os.path.basename(item)

        pkg_path = pkg_path.replace("\\", "/").lstrip("/")
        if len(pkg_path) >= NPK_PATH_MAX:
            raise ValueError(f"Archive path '{pkg_path}' too long (max {NPK_PATH_MAX - 1} chars)")

        if not os.path.isfile(host_path):
            raise FileNotFoundError(f"Host source file not found: '{host_path}'")

        mode = 0o755 if pkg_path.startswith("bin/") or pkg_path.startswith("sbin/") else 0o644
        with open(host_path, "rb") as f:
            data = f.read()

        file_entries.append({
            "path": pkg_path,
            "mode": mode,
            "size": len(data),
            "data": data
        })

    os.makedirs(os.path.dirname(os.path.abspath(output_path)), exist_ok=True)
    with open(output_path, "wb") as out:
        # Header (148 bytes)
        out.write(NPK_MAGIC)
        out.write(pack_str(name, NPK_NAME_MAX))
        out.write(pack_str(version, NPK_VER_MAX))
        out.write(pack_str(desc, NPK_DESC_MAX))
        out.write(pack_str(author, NPK_AUTH_MAX))
        out.write(struct.pack("<I", len(file_entries)))

        # File Entries
        for entry in file_entries:
            out.write(pack_str(entry["path"], NPK_PATH_MAX))
            out.write(struct.pack("<II", entry["mode"], entry["size"]))
            out.write(entry["data"])

    print(f"[MKPKG] Successfully created package '{output_path}'")
    print(f"        Name:        {name}")
    print(f"        Version:     {version}")
    print(f"        Author:      {author}")
    print(f"        Description: {desc}")
    print(f"        Files:       {len(file_entries)}")
    for entry in file_entries:
        print(f"          - {entry['path']} ({entry['size']} bytes, mode 0{entry['mode']:o})")

def info_package(pkg_path: str):
    if not os.path.isfile(pkg_path):
        print(f"Error: package file '{pkg_path}' not found", file=sys.stderr)
        return 1

    with open(pkg_path, "rb") as f:
        magic = f.read(4)
        if magic != NPK_MAGIC:
            print(f"Error: invalid NPK magic '{magic}' in '{pkg_path}'", file=sys.stderr)
            return 1

        name = unpack_str(f.read(NPK_NAME_MAX))
        ver = unpack_str(f.read(NPK_VER_MAX))
        desc = unpack_str(f.read(NPK_DESC_MAX))
        author = unpack_str(f.read(NPK_AUTH_MAX))
        file_count, = struct.unpack("<I", f.read(4))

        print(f"\nNyota Package Archive: {pkg_path}")
        print(f"  Name:        {name}")
        print(f"  Version:     {ver}")
        print(f"  Author:      {author}")
        print(f"  Description: {desc}")
        print(f"  Files:       {file_count}\n")
        print("Archived Files:")
        for _ in range(file_count):
            path = unpack_str(f.read(NPK_PATH_MAX))
            mode, size = struct.unpack("<II", f.read(8))
            print(f"  - {path:<28} {size:6d} bytes (mode 0{mode:o})")
            f.seek(size, os.SEEK_CUR)
        print()
    return 0

def main():
    parser = argparse.ArgumentParser(description="Nyota OS Package Tool")
    subparsers = parser.add_subparsers(dest="command", required=True)

    create_parser = subparsers.add_parser("create", help="Create an .npk package archive")
    create_parser.add_argument("output", help="Output .npk file path")
    create_parser.add_argument("--name", required=True, help="Package name")
    create_parser.add_argument("--version", default="1.5.0", help="Package version")
    create_parser.add_argument("--desc", default="Nyota OS Native Package", help="Short description")
    create_parser.add_argument("--author", default="Nyota Developer", help="Author/Maintainer")
    create_parser.add_argument("--file", action="append", default=[], help="File mapping: target_path=host_path")

    info_parser = subparsers.add_parser("info", help="Inspect an .npk package archive")
    info_parser.add_argument("package", help="Path to .npk file")

    args = parser.parse_args()

    if args.command == "create":
        if not args.file:
            print("Error: at least one --file is required", file=sys.stderr)
            sys.exit(1)
        create_package(args.output, args.name, args.version, args.desc, args.author, args.file)
    elif args.command == "info":
        sys.exit(info_package(args.package))

if __name__ == "__main__":
    main()
