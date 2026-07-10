# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

MiniVNC is a VNC (Remote Framebuffer) server written from scratch for vintage 68k Macintosh computers (System 7+, Macintosh Plus through color Macs, and the Apple Lisa). It is built on Classic Networking (MacTCP), not Open Transport, and is heavily optimized in C++ with hand-written 68x assembly for the hot paths. All source lives in `mac-cpp-source/`.

## Building

Two toolchains:
- **Retro68 cross-build (command-line)** — `cd retro68 && ./build.sh` for a 68k app, `ARCH=ppc ./build.sh` for a native PowerPC (CFM) app. Outputs `build[-ppc]/MiniVNC.{bin,dsk}`. Needs the Retro68 toolchain (default `~/Retro68-build/toolchain`, override with `$RETRO68`) + CMake. This works from this environment — you CAN compile. Full instructions: [`docs/BUILDING.md`](docs/BUILDING.md).
- **Metrowerks CodeWarrior IDE** (original) — the project file `mac-cpp-source/MiniVNC.µ.bin` is a MacBinary-encoded CodeWarrior project (creator `CWIE`); build on a real vintage Mac or under **Basilisk II** with CodeWarrior 8 / Symantec C++ 7. Resources live in `mac-cpp-source/MiniVNC.µ.rsrc.bin`.

Notes:
- Prefer verifying behaviour by building (Retro68) and, where possible, running on the target; reason about the code too, but the build is available.
- `.bin` files are MacBinary (preserve resource forks) — do not treat them as text or "fix" them.
- Watch for constraints of the era's compilers: no stack-local storage in interrupt-time callbacks, CodeWarrior can't mix C and assembly in one function (whole functions are written in asm), and `dprintf`/`DebugLog` is used instead of a real logger.
- PowerPC ABI gotchas bite the 68k-era code: `sizeof` of a struct ending in a sub-4-byte field after a `long` gains trailing padding (size wire messages with `offsetof`, not `sizeof`); `char` is unsigned on PPC (`-fsigned-char` is set); interrupt-time tasks (VBL) need their RoutineDescriptor in the **system heap** to run when the app is backgrounded.

## Build-time configuration

Compile-time behavior is controlled by `#define`s in `mac-cpp-source/VNCConfig.h` and `VNCServer.h`, not runtime flags:
- Feature toggles: `USE_TIGHT_AUTH`, `USE_TURBO_FEATURES`, `USE_IN_PLACE_COMPRESSION`, `VNC_COMPRESSION_LEVEL`, `VNC_HEADLESS_MODE` (auto-start when in Startup Items — the Sponsors Edition feature).
- Target-specific builds: uncommenting one `VNC_FB_RES_*` (resolution) and/or `VNC_FB_*` color-depth macro produces a binary hard-coded for one screen geometry/depth, which lets the encoders skip runtime branches. Left commented, a generic binary is built. `VNCServer.h` expands these into `VNC_FB_WIDTH/HEIGHT/BITS_PER_PIX/BYTES_PER_LINE` etc.
- Runtime user preferences (session name, port, enabled encodings, control/cursor options) are a separate `VNCConfig` struct persisted to a prefs file via `LoadPreferences`/`SavePreferences`.
- **Telemetry & debug logging** (statsd metrics + `dprintf`→UDP log sink) are opt-in and ship **off**: they arm at runtime only when a marker file (`MiniVNC Telemetry`) sits next to the app. Endpoints are in `mac-cpp-source/MetricsConfig.h` (localhost placeholders) overridable via the git-ignored `MetricsConfig.local.h`. Collector infra (k8s + docker-compose) is under `deploy/`. Full walkthrough: [`docs/TELEMETRY.md`](docs/TELEMETRY.md).

## Architecture

The server is fundamentally an **asynchronous state machine driven by MacTCP completion routines**, not threads. Because callbacks run at interrupt time, there is essentially no stack-local state across steps — nearly all connection state lives in globals declared in `VNCServer.h` (`vncState`, `vncConfig`, `vncFlags`, the TCP parameter blocks `epb_send`/`epb_recv`, etc.). When editing control flow, follow the completion-routine chaining rather than expecting linear functions.

Key pieces:
- **`main.cpp`** — Classic Mac app shell: menus, the main/options/log dialogs, event loop, and idle-task pumping. Dialog item IDs are enums at the top of the file.
- **`VNCServer.cpp`** — the protocol engine and connection state machine (`vncServerStart/Stop/IdleTask`, handshake, auth, client/server message dispatch). Largest and most central file.
- **`ChainedTCPHelper.{cpp,h}`** — thin wrapper over MacTCP async parameter blocks that lets completion routines be chained. This is the concurrency primitive.
- **`VNCFrameBuffer.{cpp,h}`** — access to the live screen via the low-memory global `ScrnBase`.
- **`VNCScreenHash.cpp`** — inexact screen-change detection using per-row and per-column 32-bit sums (see README "Screen Change Detection via Checksums"). Determines the dirty rectangle; deliberately trades accuracy for speed/memory.
- **`VNCPalette*.cpp`** — color map / true-color handling.
- **Encoders** — dispatched through `VNCEncoder.{cpp,h}` (`selectedEncoder`, `begin()`, `getChunk()`, `getCompressedChunk()`), which picks an implementation from the client's supported encodings and the build's color depth:
  - `VNCEncodeTRLE.cpp` / `VNCEncodeTRLEMono.cpp` — TRLE, the only encoding usable for 1-bit B&W Macs.
  - `VNCEncodeHextile.cpp`, `VNCEncodeZRLE.cpp`, `VNCEncodeRAW.cpp`, `VNCEncodeTight.cpp` — for color-capable clients/Macs.
  - `VNCEncodeCursor.cpp` — cursor pseudo-encoding.
  - Tile encoding core is split: **`VNCEncodeTilesASM.cpp`** (68x assembly, gated by `USE_ASM_CODE` in `VNCEncodeTiles.h`) and **`VNCEncodeTilesC.cpp`** (portable C reference). Both implement the same `screenToNative`/`nativeToRle`/`nativeToPacked`/`nativeToColors` interface — keep them in sync when changing tile logic.
  - `VNCEncoderZLib.cpp` wraps zlib (via `libs/miniz-3.0.2`) for ZRLE/Tight compression.
- **`TightVNCSupport.cpp` / `TightVNCTypes.*`** — TightVNC extensions: VNC/Tight auth and file uploads.
- **`VNCTypes.h`** — RFB protocol message structs and encoding constants.
- **`VNCStreamReader.*`** — incremental parsing of inbound client messages arriving across TCP reads.

### Vendored libraries (`mac-cpp-source/libs/`)
- `miniz-3.0.2/` — zlib-compatible compressor (upstream, do not edit lightly).
- `MacTCP.h`, `compat.h` — Classic Mac networking/OS headers.
- `Ari Halberstadt/OSUtilities.*`, `Common Libs/GestaltUtils.*` — borrowed OS-utility helpers.
- `ChromiVNC/VNCKeyboard.*`, `keysymdef.h` — keyboard/keysym mapping adapted from ChromiVNC.

## Domain notes worth knowing before editing

These are the hard-won hacks documented in the README's "Technical Notes" (read that section for full context):
- **Mouse button control** relies on manipulating the low-memory globals `MBState` and `MBTicks` to keep the ROM VIA interrupt from overwriting the button state — required to make dragging/menu selection work on a Macintosh Plus.
- **Change rectangles are aligned to byte (and, with 32-bit column sums, 32-pixel) boundaries** so 1-bit screen data can be copied without bit-shifting/masking, which is prohibitively slow on the 68000.
- **TRLE is used even where it violates the VNC spec** because its paletted 1/2/4-bit tile types are the only way to ship sub-8-bit pixels without expanding them to full color bytes.
- Performance is the overriding design constraint; correctness/accuracy is knowingly sacrificed (missed screen updates, occasional artifacts) to keep the 68000 fast.
