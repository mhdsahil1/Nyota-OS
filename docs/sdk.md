# Nyota OS Native Developer SDK Guide

**Target Version:** `v1.5.0`  
**Toolchain:** GCC / Clang (Freestanding x86_64), GNU LD, NASM  
**Target Architecture:** x86_64-elf

---

## 1. Introduction

The **Nyota OS Developer SDK** provides everything required to compile, link, and package native C applications for Nyota OS.

The SDK consists of:
- **`sdk/include/`**: C standard library headers (`libnyota.h`, `types.h`).
- **`sdk/lib/`**: Runtime startup object (`crt0.o`) and static C library (`libnyota.a`).
- **`sdk/user.ld`**: Standard linker script mapping user applications to virtual base `0x0000008000000000ULL`.
- **`tools/nyotacc`**: Compiler driver script abstracting toolchain arguments.
- **`tools/mkpkg.py`**: Packaging utility creating `.npk` archives for deployment.

---

## 2. Compilation Workflow

### Recommended Method: `nyotacc` Driver

The `nyotacc` driver automatically supplies the required freestanding flags, include paths, entry stub, and linker script:

```bash
# Compile and link a single C file
python tools/nyotacc.py -o hello hello.c

# Compile multiple C source files
python tools/nyotacc.py -o myapp main.c util.c math.c

# Enable verbose compilation commands
python tools/nyotacc.py -v -o myapp main.c
```

### Manual Compilation

For custom Makefiles or build systems:

```bash
# 1. Compile C sources
gcc -m64 \
    -ffreestanding \
    -fno-stack-protector \
    -fno-pie \
    -fno-pic \
    -mno-red-zone \
    -Wall \
    -Wextra \
    -Isdk/include \
    -c main.c -o main.o

# 2. Link with crt0.o and libnyota.a
ld -nostdlib \
   -m i386pep \
   --image-base 0x0000008000000000 \
   -T sdk/user.ld \
   -o main.pe \
   sdk/lib/crt0.o main.o sdk/lib/libnyota.a

# 3. Convert PE executable to static ELF64
objcopy -O elf64-x86-64 main.pe main
```

---

## 3. Sample Application Code

```c
#include "libnyota.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    printf("Hello from a native Nyota OS C application!\n");

    // Dynamic memory allocation
    char *buf = (char *)malloc(128);
    if (buf) {
        snprintf(buf, 128, "Allocated heap memory at %p\n", buf);
        printf("%s", buf);
        free(buf);
    }

    // Directory traversal
    DIR *d = opendir("/bin");
    if (d) {
        printf("Listing /bin directory:\n");
        dirent_t *ent;
        int count = 0;
        while ((ent = readdir(d)) != NULL && count < 5) {
            printf("  - %s\n", ent->d_name);
            count++;
        }
        closedir(d);
    }

    return 0;
}
```

---

## 4. Packaging and Deployment

Once compiled, applications can be packaged using `tools/mkpkg.py`:

```bash
python tools/mkpkg.py create myapp.npk \
    --name myapp \
    --version 1.0.0 \
    --desc "My Sample Native Application" \
    --author "Nyota Developer" \
    --file bin/myapp=myapp
```

To install inside Nyota OS:
```sh
nyotapkg install /pkg/myapp.npk
myapp
```
