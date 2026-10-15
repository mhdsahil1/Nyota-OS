# Nyota OS Developer Platform SDK (v1.5.0)

Welcome to the **Nyota OS Native Developer Platform SDK**. This software development kit provides header files, static libraries, runtime startup objects, and linker scripts to compile native 64-bit C applications for Nyota OS.

---

## 1. Directory Structure

```
sdk/
├── include/
│   ├── libnyota.h       # Nyota OS C standard library & system call interfaces
│   └── types.h          # Fixed-width integer and standard platform types
├── lib/
│   ├── crt0.o           # Ring 3 userspace runtime entry stub (_start -> main)
│   └── libnyota.a       # Static libc archive (malloc, io, string, dir, sys)
├── user.ld              # Base userspace linker script (loads at 0x8000000000)
└── README.md            # This reference manual
```

---

## 2. Quickstart: Compiling a Native Application

Use the cross-compiler driver `tools/nyotacc` to compile any native C application:

```bash
# Using the nyotacc driver
python tools/nyotacc.py -o myapp myapp.c

# Or using the shell/batch wrappers
./tools/nyotacc -o myapp myapp.c       # Linux / macOS / Git Bash
tools\nyotacc.bat -o myapp.elf myapp.c  # Windows CMD
```

### Manual GCC / Clang Command

If invoking the toolchain directly:

```bash
# 1. Compile C source to object
gcc -m64 -ffreestanding -fno-stack-protector -fno-pie -fno-pic -mno-red-zone \
    -Wall -Wextra -Isdk/include -c myapp.c -o myapp.o

# 2. Link against SDK crt0.o and libnyota.a
ld -nostdlib -m i386pep --image-base 0x0000008000000000 \
    -T sdk/user.ld -o myapp.pe sdk/lib/crt0.o myapp.o sdk/lib/libnyota.a

# 3. Convert intermediate PE to static ELF64
objcopy -O elf64-x86-64 myapp.pe myapp
```

---

## 3. Packaging Applications with `nyotapkg`

Nyota OS includes a native package manager and host packager (`tools/mkpkg.py`):

```bash
# Create a .npk archive on the host
python tools/mkpkg.py create myapp.npk \
    --name myapp \
    --version 1.0.0 \
    --desc "My Native Nyota Application" \
    --author "Developer" \
    --file bin/myapp=myapp

# Inside Nyota OS:
nyotapkg info /pkg/myapp.npk      # Inspect package contents
nyotapkg install /pkg/myapp.npk   # Validate and install
nyotapkg list                     # List all installed packages
nyotapkg remove myapp             # Uninstall package and cleanly remove files
```

---

## 4. C Standard Library (`libnyota`) APIs

### Dynamic Memory Management
- `void *malloc(size_t size);` — 16-byte aligned dynamic memory allocation.
- `void free(void *ptr);` — Releases chunks and coalesces adjacent free memory.
- `void *calloc(size_t nmemb, size_t size);` — Zero-initialized allocation.
- `void *realloc(void *ptr, size_t size);` — Memory block resizing with data preservation.

### Directory Streaming (POSIX-inspired)
- `DIR *opendir(const char *name);`
- `dirent_t *readdir(DIR *dirp);`
- `void rewinddir(DIR *dirp);`
- `int closedir(DIR *dirp);`

### String & Parsing Utilities
- `char *strdup(const char *s);`, `char *strndup(const char *s, size_t n);`
- `char *strtok(char *str, const char *delim);`, `char *strtok_r(...);`
- `long strtol(const char *nptr, char **endptr, int base);`, `long atol(...);`
- `int memcmp(const void *s1, const void *s2, size_t n);`
- `size_t strspn(...);`, `size_t strcspn(...);`
- Full ctype suite: `isalpha`, `isdigit`, `isspace`, `isalnum`, `isprint`, `toupper`, `tolower`.

### Formatted I/O
- `int snprintf(char *str, size_t size, const char *format, ...);`
- `int vsnprintf(char *str, size_t size, const char *format, va_list ap);`
- `int dprintf(int fd, const char *format, ...);`
- `int fprintf(int fd, const char *format, ...);`

### System Calls & File I/O
- File operations: `open`, `close`, `read`, `write`, `seek`, `stat`, `access`, `unlink`.
- Process & IPC: `getpid`, `spawn`, `spawn2`, `waitpid`, `pipe`, `dup2`, `kill`, `signal`.
- Network & Socket: `socket`, `bind`, `listen`, `accept`, `connect`, `send`, `recv`.
- Memory & Heap: `brk`, `sbrk`.

---

## 5. Architectural Constraints

> [!NOTE]
> Nyota OS v1.5.0 uses **static 64-bit ELF loading**. Dynamic linking (`.so`) and runtime relocation are currently not supported; all shared library and dynamic linking features remain deliberately disabled until full memory mapping and relocation subsystems are completed.
