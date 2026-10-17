# Nyota OS Developer Platform SDK (v1.6.0)

Welcome to the **Nyota OS Native Developer Platform SDK**. This software development kit provides header files, static libraries, GUI toolkits, runtime startup objects, example code, and linker scripts to compile native 64-bit C applications for Nyota OS.

---

## 1. Directory Structure

```
sdk/
├── include/
│   ├── libnyota.h       # Nyota OS C standard library & system call interfaces
│   ├── gui.h            # Native GUI Toolkit (libgui) public API & widget tree
│   ├── theme.h          # Design tokens, color palette, and UI metrics
│   └── types.h          # Fixed-width integer and standard platform types
├── lib/
│   ├── crt0.o           # Ring 3 userspace runtime entry stub (_start -> main)
│   ├── libnyota.a       # Static libc archive (malloc, io, string, dir, sys)
│   └── libgui.a         # Static GUI toolkit archive (windows, widgets, render)
├── examples/
│   ├── hello_cli.c      # Example console/CLI application
│   ├── hello_gui.c      # Example interactive graphical window application
│   └── Makefile         # Standalone build script for example applications
├── user.ld              # Base userspace linker script (loads at 0x8000000000)
└── README.md            # This reference manual
```

---

## 2. Quickstart: Compiling Native Applications

### A. Using the `nyotacc` Cross-Compiler Driver

The easiest way to compile applications is via `tools/nyotacc.py`:

```bash
# Compile a CLI application
python tools/nyotacc.py -o hello_cli sdk/examples/hello_cli.c

# Compile a GUI application (automatically links libgui.a and libnyota.a)
python tools/nyotacc.py -o hello_gui sdk/examples/hello_gui.c

# Platform wrappers
./tools/nyotacc -o myapp myapp.c          # Linux / macOS / Git Bash
tools\nyotacc.bat -o myapp.elf myapp.c     # Windows CMD
```

### B. Manual Toolchain Invocation

For custom build systems or Makefiles:

```bash
# 1. Compile C source to object
gcc -m64 -ffreestanding -fno-stack-protector -fno-pie -fno-pic -mno-red-zone \
    -Wall -Wextra -Isdk/include -c myapp.c -o myapp.o

# 2. Link against SDK startup code and libraries
# For CLI apps:
ld -nostdlib -m i386pep --image-base 0x0000008000000000 \
    -T sdk/user.ld -o myapp.pe sdk/lib/crt0.o myapp.o sdk/lib/libnyota.a

# For GUI apps (include libgui.a before libnyota.a):
ld -nostdlib -m i386pep --image-base 0x0000008000000000 \
    -T sdk/user.ld -o myapp.pe sdk/lib/crt0.o myapp.o sdk/lib/libgui.a sdk/lib/libnyota.a

# 3. Extract static ELF64 executable
objcopy -O elf64-x86-64 myapp.pe myapp
```

---

## 3. Native GUI Toolkit (`libgui`)

Nyota OS includes a unified, lightweight GUI toolkit (`libgui`) supporting window management, 2D software rendering, event handling, and a composited widget hierarchy.

### Window Lifecycle
- `gui_window_t *gui_window_create(uint32_t width, uint32_t height, const char *title);`
  Registers a client window with the desktop compositor and allocates a double-buffered local pixel surface.
- `void gui_window_destroy(gui_window_t *win);`
  Closes the window, notifies the window server, and releases local memory.
- `void gui_window_update(gui_window_t *win);`
  Flushes the local pixel buffer to the kernel window server for desktop compositing.
- `bool gui_window_poll_event(gui_window_t *win, input_event_t *ev);`
  Retrieves pending mouse, keyboard, or window lifecycle events without blocking.

### Drawing Primitives
- `gui_fill_rect(win, x, y, w, h, color);` — Fills a rectangular region.
- `gui_draw_rect(win, x, y, w, h, color);` — Draws an outlined rectangular border.
- `gui_draw_line(win, x0, y0, x1, y1, color);` — Bresenham 2D line.
- `gui_draw_text(win, x, y, text, fg, bg);` — Monospace bitmap text rendering.

### Standard Widgets (`gui_widget_t`)
`libgui` provides pre-styled widgets with automatic state management and event dispatch:
- **Buttons (`WIDGET_BUTTON`)**: Click handling with normal, hover, and pressed states.
- **Labels (`WIDGET_LABEL`)**: Read-only text display with configurable typography tokens.
- **Text Boxes (`WIDGET_TEXTBOX`)**: Editable single-line text entry with cursor support.
- **Lists (`WIDGET_LIST`)**: Scrollable multi-item selection list.
- **Menus (`WIDGET_MENU`)**: Dropdown and popup menu containers.
- **Checkboxes (`WIDGET_CHECKBOX`)**: Toggleable boolean option controls.
- **Progress Bars (`WIDGET_PROGRESSBAR`)**: Visual completion indicators.
- **Scrollbars (`WIDGET_SCROLLBAR`)**: Horizontal and vertical viewport tracking.

### Theme & Design Tokens (`theme.h`)
The Nyota design system employs a curated dark-space palette:
- `COLOR_WINDOW_BG` (`0xFF1E1E2E`): Base window surface.
- `COLOR_WIDGET_BG` (`0xFF24273A`): Input containers and list cards.
- `COLOR_ACCENT_PRIMARY` (`0xFF89B4FA`): Primary interactive accent (Nyota Blue).
- `COLOR_TITLE_ACTIVE` (`0xFFCDD6F4`): Primary high-contrast text.
- `COLOR_TEXT_MUTED` (`0xFFA6ADC8`): Secondary text.
- `COLOR_SUCCESS` (`0xFFA6E3A1`), `COLOR_WARNING` (`0xFFF9E2AF`), `COLOR_DANGER` (`0xFFF38BA8`).

---

## 4. Packaging Applications with `nyotapkg`

Nyota OS includes a package manager and host packager (`tools/mkpkg.py`):

```bash
# Create a .npk archive on the host
python tools/mkpkg.py create myapp.npk \
    --name myapp \
    --version 1.0.0 \
    --desc "My Native Nyota Application" \
    --author "Developer" \
    --file bin/myapp=myapp

# Inside Nyota OS:
nyotapkg info /pkg/myapp.npk      # Inspect package contents & checksum
nyotapkg install /pkg/myapp.npk   # Validate signature and install into /bin
nyotapkg list                     # List all installed packages
nyotapkg remove myapp             # Cleanly uninstall package and remove binaries
```

---

## 5. C Standard Library (`libnyota`) APIs

### Dynamic Memory Management
- `void *malloc(size_t size);` — 16-byte aligned dynamic memory allocation.
- `void free(void *ptr);` — Releases chunks and coalesces adjacent free memory.
- `void *calloc(size_t nmemb, size_t size);` — Zero-initialized allocation.
- `void *realloc(void *ptr, size_t size);` — Memory block resizing with data preservation.

### Directory Streaming (POSIX-compatible)
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
- Character classification: `isalpha`, `isdigit`, `isspace`, `isalnum`, `isprint`, `toupper`, `tolower`.

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

## 6. Architectural Constraints

> [!NOTE]
> Nyota OS v1.6.0 uses **static 64-bit ELF loading**. Dynamic linking (`.so`) and runtime relocation are currently not supported; all shared library and dynamic linking features remain deliberately disabled until full memory mapping and relocation subsystems are completed.
