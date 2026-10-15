# Nyota OS Developer Platform Architecture

**Target Release:** `v1.5.0`  
**Architecture:** x86_64  
**Primary Subsystems:** C Standard Library (`libnyota`), Dynamic Memory Management, System Call ABI, Executable Loader, Toolchain Driver, Package Management (`nyotapkg`)

---

## 1. Executive Overview

Nyota OS v1.5.0 transforms Nyota OS into an open, developer-accessible platform where native C applications can be developed, compiled, packaged, installed, and run with a reproducible workflow.

Phase 15 delivers five foundational pillars:
1. **C Standard Library (`libnyota`)**: A modern standard C library providing 16-byte aligned dynamic heap allocation (`malloc`, `free`, `calloc`, `realloc`), POSIX-inspired directory streaming (`opendir`, `readdir`, `closedir`, `rewinddir`), string tokenization and numerical conversion (`strdup`, `strtok_r`, `strtol`, `atol`), ctype classification, and formatted I/O (`snprintf`, `vsnprintf`, `dprintf`, `fprintf`).
2. **System APIs & Heap Management**: Kernel support for `SYS_BRK` (85) and `SYS_SBRK` (86) system calls managing process heap expansion from `USER_HEAP_BASE` (`0x0000008002000000ULL`) up to the user stack boundary, featuring automatic physical page allocation, page mapping (`PAGE_PRESENT | PAGE_USER | PAGE_WRITABLE | PAGE_NX`), and cleanup on process termination.
3. **Executable Format & Linking**: Hardened static 64-bit ELF loading verifying entry point execution permissions, enforcing BSS zeroing, and linking via `crt0.o` runtime entry.
4. **Reproducible Compiler Toolchain**: The `tools/nyotacc` driver and standalone SDK (`sdk/`) providing turn-key native compilation for host developers.
5. **Native Package Manager (`nyotapkg`)**: The `/bin/nyotapkg` user utility and host `tools/mkpkg.py` tool for generating, inspecting, installing, listing, and cleanly uninstalling `.npk` package archives with strict path traversal defenses.

---

## 2. Address Space & User Heap Architecture

Nyota OS maintains a strictly separated virtual address space layout for Ring 3 applications:

```
+-------------------------------------------------------------------------+
| Virtual Address Range                | Usage & Memory Attributes        |
+-------------------------------------------------------------------------+
| 0x0000008000000000 - 0x0000008001FFFFFF | User Code & Data (ELF segments)  |
|                                      | Code: R-X (PAGE_PRESENT | USER)   |
|                                      | Data/BSS: RW- (PAGE_NX | WRITABLE)|
+-------------------------------------------------------------------------+
| 0x0000008002000000 - 0x000000800EEFFFFF | Dynamic User Heap (brk / sbrk)   |
|                                      | Attributes: RW- (PAGE_NX | USER)  |
|                                      | Allocated on-demand by sys_brk    |
+-------------------------------------------------------------------------+
| 0x000000800EF00000 - 0x000000800EFFFFFF | Guard Page Zone (Unmapped)        |
+-------------------------------------------------------------------------+
| 0x000000800F000000 - 0x000000800FF00000 | User Execution Stack              |
|                                      | Grows downward towards guard zone |
|                                      | Attributes: RW- (PAGE_NX | USER)  |
+-------------------------------------------------------------------------+
```

### Heap Mechanics (`SYS_BRK` & `SYS_SBRK`)
- `heap_start`: Initialized at `0x0000008002000000ULL` on process creation.
- `heap_end`: Current break point, page-aligned when requesting memory from the Physical Memory Manager (PMM).
- When `sbrk(inc)` is invoked:
  - If `inc > 0`: Verifies `new_brk` does not breach `USER_STACK_TOP - 0x100000`. Allocates physical 4 KiB frames from PMM, maps them with `PAGE_PRESENT | PAGE_USER | PAGE_WRITABLE | PAGE_NX`, and zeros newly allocated pages.
  - If `inc < 0`: Shrinks break point and unmaps/frees released physical frames back to the PMM pool.
  - On `process_exit()`: The kernel iterates over all mapped heap pages between `heap_start` and `heap_end`, freeing physical frames and unmapping page table entries.

---

## 3. C Standard Library (`libc`) Enhancements

### Dynamic Memory Allocator (`user/libnyota/malloc.c`)
- **Chunk Headers**: Every allocation is preceded by an 8-byte chunk header storing size and allocation flag:
  - Size is always aligned to 16 bytes (`CHUNK_ALIGN = 16`).
  - Bit 0 (`CHUNK_ALLOCATED = 0x1`) marks busy blocks.
- **Allocation Strategy**: First-fit search across existing free blocks in the circular heap chunk list.
- **Chunk Splitting**: If a free chunk exceeds the requested size by at least `sizeof(chunk_header_t) + 16`, it is split into an allocated chunk and a residual free chunk.
- **Bidirectional Coalescing**: When `free(ptr)` is called, adjacent free chunks before and after the freed block are merged immediately to eliminate external heap fragmentation.
- **API Coverage**:
  - `malloc(size_t size)`
  - `free(void *ptr)`
  - `calloc(size_t nmemb, size_t size)` (zero-initialized)
  - `realloc(void *ptr, size_t size)` (preserves existing data, reallocates if necessary)

### POSIX-Inspired Directory Streaming (`user/libnyota/dir.c`)
- Implements directory streaming using underlying `sys_open(path, O_RDONLY)` and `sys_getdents(fd, dirp, 1)`.
- Compatible data structure with anonymous union:
  - Supports both standard POSIX names (`d_name`, `d_ino`, `d_type`) and NyotaFS native names (`name`, `inode`, `type`).
- APIs:
  - `DIR *opendir(const char *name)`
  - `dirent_t *readdir(DIR *dirp)`
  - `void rewinddir(DIR *dirp)`
  - `int closedir(DIR *dirp)`

### String, CType & Formatting Utilities
- `strdup(s)` / `strndup(s, n)`
- `strtok(s, delim)` / `strtok_r(s, delim, saveptr)`
- `strtol(nptr, endptr, base)` / `atol(nptr)`
- `memcmp(s1, s2, n)` / `strspn(s, accept)` / `strcspn(s, reject)`
- `isalpha`, `isdigit`, `isspace`, `isalnum`, `isprint`, `isupper`, `islower`, `toupper`, `tolower`
- `snprintf`, `vsnprintf`, `fprintf`, `dprintf`, `access(path, mode)`

---

## 4. Executable Format & Linking Model

### Static Linking Architecture
Nyota OS executes **static 64-bit ELF (Executable and Linkable Format)** binaries:
1. **Freestanding Compilation**: Programs are compiled with `-m64 -ffreestanding -fno-pie -fno-pic -mno-red-zone`.
2. **Runtime Initialization**: Execution starts at `_start` in `crt0.o`, which sets up stack alignment, extracts `argc`/`argv`/`environ`, and transfers control to `main(argc, argv)`.
3. **Exit Handling**: If `main()` returns, `_start` invokes `exit(status)` to ensure clean process termination and resource reclamation.
4. **ELF Loader Hardening**:
   - The kernel validates that `ehdr.e_entry` resides strictly within a loadable segment (`PT_LOAD`) marked executable (`PF_X`).
   - BSS segments are cleared to zero before entry.
   - Non-executable segments (`data`, `rodata`, `bss`) are mapped with `PAGE_NX`.

> [!NOTE]
> **Dynamic Linking Status:** Dynamic linking (`.so` shared libraries) is intentionally **not supported** in v1.5.0. Nyota OS strictly uses static ELF linking until virtual memory demand-paging and dynamic relocation loaders are implemented.

---

## 5. Native Package Management (`nyotapkg`)

### Package Archive Format (`.npk`)
Nyota Package files use a compact binary format:

```
+-------------------------------------------------------------------+
| Magic ("NPK1") (4 bytes)                                          |
+-------------------------------------------------------------------+
| Package Name [32 bytes]  | Version [16 bytes]                     |
+-------------------------------------------------------------------+
| Description [64 bytes]   | Author [32 bytes]                      |
+-------------------------------------------------------------------+
| File Count (4 bytes uint32)                                       |
+===================================================================+
| Repeated for each file:                                           |
|   - Relative Path [64 bytes] (e.g. "bin/devdemo")                 |
|   - File Mode (4 bytes uint32)                                    |
|   - Payload Size (4 bytes uint32)                                 |
|   - Payload Data [Size bytes]                                     |
+-------------------------------------------------------------------+
```

### Security & Path Traversal Mitigations
- **Name Validation**: Only `[a-zA-Z0-9_-]` characters permitted.
- **Path Sanitization**:
  - Rejects any path containing `..`.
  - Rejects leading `/` or `\\` characters.
  - Rejects drive colon `:` characters.
  - Whitelists target prefixes: only paths under `bin/`, `etc/`, `share/`, `lib/`, or `usr/` are permitted.
- **Package Database**:
  - Installed package manifests are stored at `/var/lib/pkg/installed/<name>.list`.
  - Manifest records package metadata and every installed file path.
  - When uninstalling (`nyotapkg remove <name>`), each file in the manifest is unlinked before removing the manifest.

### Command Reference
```bash
nyotapkg info <pkg.npk>     # Inspect package metadata, file count, and sizes
nyotapkg install <pkg.npk>  # Validate and extract package files, register manifest
nyotapkg list               # List all installed packages in /var/lib/pkg/installed
nyotapkg remove <name>      # Remove all files associated with the package
```

---

## 6. Verification & Automated Testing

The native developer platform is verified through two test suites:
1. `/bin/devdemo`: In-guest native C test suite verifying malloc alignment, memory pattern integrity, calloc, realloc, chunk coalescing, 64 KiB heap expansion, string utilities, ctype, formatting, directory streaming, and file I/O.
2. `tools/test_phase15.py`: Host-driven QEMU automated test verifying OS boot, `uname -a` (v1.5.0), `/bin/devdemo` execution, `nyotapkg info`, `nyotapkg install`, `nyotapkg list`, `nyotapkg remove`, and security regression testing.
