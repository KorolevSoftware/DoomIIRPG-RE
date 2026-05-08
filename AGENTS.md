# Agent Instructions for Doom II RPG (DoomIIRPG)

## Project Overview

**Doom II RPG** is a Windows/desktop port of the J2ME/BREW mobile game *Doom II RPG* by GEC (Erick Vásquez García). The project is a reverse engineering effort that reimplements the original game using C++17 with SDL2 for windowing/input, OpenGL/TinyGL for rendering, and OpenAL for audio.

### Key Facts
- **Original platform**: J2ME / BREW (mobile)
- **Target platform**: Windows (MSVC), with partial UNIX support
- **Original resolution**: 480×320 (fixed logical resolution, windowed scaling)
- **Game engine**: Custom BSP renderer (TinyGL-based) with optional OpenGL path (GLES)
- **Primary target**: `DoomIIRPG` (executable)

---

## Build / Lint / Test Commands

### Building the Project

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/sdl2 && cmake --build build
```

**Single target**: `DoomIIRPG`

> **Important**: The project uses a custom `cmake/Modules/FindSDL2.cmake` that does **not** automatically pick up `SDL2_DIR`. You must pass `CMAKE_PREFIX_PATH` or set the `SDL2DIR` environment variable.

#### Windows Example (full paths)

```bash
cmake -S . -B build ^
    -DCMAKE_PREFIX_PATH=g:/Projects/SDL2-2.32.8 ^
    -DZLIB_INCLUDE_DIR=g:/Projects/msvc2017_64/include/zlib ^
    -DZLIB_LIBRARY=g:/Projects/msvc2017_64/lib/zlib/zlib.lib ^
    -DOPENAL_INCLUDE_DIR="c:/Program Files (x86)/OpenAL 1.1 SDK/include" ^
    -DOPENAL_LIBRARY="c:/Program Files (x86)/OpenAL 1.1 SDK/libs/Win64/OpenAL32.lib" ^
    && cmake --build build
```

#### Unix/Linux Example

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/usr/local && cmake --build build
```

### Dependencies

| Dependency | Purpose |
|------------|---------|
| **SDL2** | Windowing, input, audio backend |
| **ZLIB** | Archive/zip file handling |
| **OpenAL** | Audio playback |
| **OpenGL** | 3D rendering (optional GLES path) |
| **hash-library** | Third-party hashing (static lib) |

### CMake Configuration

- **Minimum CMake version**: 3.22
- **Compiler**: C++17 required
- **Custom CMake modules**: `cmake/Modules/FindSDL2.cmake`, `cmake/Modules/FindSDL2_mixer.cmake`

---

## Linting

### clang-format

The project uses clang-format 18 with LLVM style:

```bash
clang-format -i $(git ls-files | grep '\.cpp\|\.h$')
```

**.clang-format** (LLVM style):
```
BasedOnStyle: LLVM
IndentWidth: 4
UseTab: Never
ColumnLimit: 120
AllowShortIfStatementsOnASingleLine: false
```

### clang-tidy

```bash
clang-tidy $(git ls-files | grep '\.cpp$') --
```

---

## Code Style Guidelines

### General Conventions
- **Language**: C++17 (or newer if the compiler supports it)
- **Header Guards**: Use `#pragma once` or traditional guards (`#ifndef __X_H__`)
- **Includes**:
  - System headers first, then project headers
  - Prefer angle brackets for system headers (`<vector>`) and quotes for local ones (`"App.h"`)
  - Order alphabetically within each group

### Formatting
- Use `clang-format` with the configuration above
- Run `clang-format -i` before committing

### Types & Naming

| Element | Convention | Example |
|---------|-----------|---------|
| Variables | `snake_case`, lower-case | `screen_width` |
| Member Variables | `m_` prefix | `m_width` |
| Private Member Variables | `m_` prefix | `m_screenWidth` |
| Constants / Enums | `UPPER_SNAKE_CASE` | `MAX_ENTITIES` |
| Functions / Methods | `camelCase`, lowercase start | `loadMap()`, `renderSprite()` |
| Classes / Structs | `PascalCase` | `Render`, `TinyGL` |
| Static Members | `snake_case` or `camelCase` | `IOS_WIDTH`, `shiftStretch` |

### Error Handling
- Prefer exceptions for unrecoverable errors
- Return `std::optional` or error codes for recoverable ones
- Do not swallow exceptions; always rethrow or log
- Use RAII for resource management (smart pointers, containers)

### Comments & Documentation
- Document public APIs with Doxygen-style comments
- Inline comments should explain **why**, not **what**
- Look for `[GEC]` prefixed comments — these are author notes from the original port

---

## Project Structure

```
DoomIIRPG-RE/
├── CMakeLists.txt              # Root CMake file
├── README.md                   # Project README
├── AGENTS.md                   # This file
├── cmake/
│   └── Modules/
│       ├── FindSDL2.cmake      # Custom SDL2 finder
│       └── FindSDL2_mixer.cmake
├── src/
│   ├── Main.cpp                # Entry point (main function)
│   ├── App.h / App.cpp         # Core application singleton (Applet)
│   ├── CAppContainer.h/.cpp    # Application container (singleton)
│   ├── Canvas.h/.cpp           # Canvas/state machine, game states
│   ├── Render.h/.cpp           # BSP renderer, sprite rendering
│   ├── TinyGL.h/.cpp           # TinyGL software renderer
│   ├── GLES.h/.cpp             # OpenGL rendering path
│   ├── Game.h/.cpp             # Game logic, entities, scripts
│   ├── Entity.h/.cpp           # Entity base class
│   ├── EntityMonster.h/.cpp    # Monster entity
│   ├── Player.h/.cpp           # Player state
│   ├── Combat.h/.cpp           # Combat system
│   ├── Hud.h/.cpp              # HUD rendering
│   ├── MenuSystem.h/.cpp       # Menu system
│   ├── Input.h/.cpp            # Input handling
│   ├── Sound.h/.cpp            # Audio system
│   ├── Graphics.h/.cpp         # Graphics primitives
│   ├── Image.h/.cpp            # Image loading
│   ├── SDLGL.h/.cpp            # SDL window + OpenGL context
│   ├── ZipFile.h/.cpp          # ZIP archive handling
│   ├── Enums.h                 # All game enums/constants
│   ├── VendingMachine.h/.cpp   # Vending machine minigame
│   ├── SentryBotGame.h/.cpp    # Sentry Bot minigame
│   ├── HackingGame.h/.cpp      # Hacking minigame
│   ├── ComicBook.h/.cpp        # Comic book minigame
│   └── ...
└── third_party_libs/
    └── hash-library/           # Third-party hash library
```

### Core Classes

| Class | Responsibility |
|-------|---------------|
| `Applet` | Core game state, singleton, image/resource loading |
| `CAppContainer` | Singleton container, app lifecycle |
| `Canvas` | Game state machine (menu, playing, combat, etc.) |
| `Render` | BSP rendering, sprite management, media mapping |
| `TinyGL` | Software renderer (fixed-point math) |
| `GLES` | OpenGL rendering path |
| `Game` | Game logic, entities, scripts, maps |
| `Player` | Player stats, inventory, weapons |
| `MenuSystem` | Menu navigation and rendering |
| `Input` | Keyboard/touch input abstraction |
| `Sound` | Audio playback via OpenAL |

---

## Testing

The project currently has **no test suite**. Tests can be added with Catch2:

```bash
cmake -S . -B build -DBUILD_TESTS=ON && cmake --build build
./build/tests/doom_rpg_tests
```

---

## Platform Notes

### Windows (MSVC)
- Uses `/D_CRT_SECURE_NO_WARNINGS` by default
- `-fsigned-char` is passed but produces a warning on MSVC (safe to ignore)
- Output: `build/src/Debug/DoomIIRPG.exe`

### UNIX
- Uses `-Wno-write-strings` by default
- Requires SDL2, ZLIB, OpenAL installed via package manager

---

## Cheat Codes (from original J2ME/BREW version)

Enter via menu:
| Code | Effect |
|------|--------|
| `3666` | Opens debug menu |
| `1666` | Restarts level |
| `4332` | Gives all keys, items, and weapons |
| `3366` | Starts speed test (Benchmark) |

---

## Cursor Rules

Check `.cursor/rules/` directory. If present, include relevant rules in this file.

## Copilot Instructions

If `.github/copilot-instructions.md` exists, summarize its key points here.