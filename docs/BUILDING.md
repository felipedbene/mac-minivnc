# Building MiniVNC

MiniVNC can be built two ways. The original toolchain is the Metrowerks
**CodeWarrior** IDE (see the upstream README's *Technical Notes*). This fork adds
a **command-line cross-build** using the open-source [Retro68] toolchain, which
produces both a **68k** app and a **native PowerPC (CFM)** app from the same
sources — no vintage Mac or emulator needed to compile.

[Retro68]: https://github.com/autc04/Retro68

## Prerequisites

- **Retro68** built and installed (provides the GCC cross-compilers, `Rez`,
  `MakeAPPL`, and the CMake toolchain files). Follow Retro68's own build
  instructions; by default it installs to `~/Retro68-build/toolchain`.
- **CMake** ≥ 3.10 and a host C/C++ toolchain (to run the build).

## Quick start

```sh
cd retro68

./build.sh            # 68k app      -> retro68/build/MiniVNC.{bin,dsk}
ARCH=ppc ./build.sh   # PowerPC app  -> retro68/build-ppc/MiniVNC.{bin,dsk}
```

Each build produces:

- `MiniVNC.bin` — the application, MacBinary-encoded (preserves the resource
  fork + type/creator). Transfer it to a Mac and decode with Stuffit Expander,
  or copy it onto the `.dsk` image.
- `MiniVNC.dsk` — a mountable disk image containing the app, ready to drop into
  Basilisk II / SheepShaver or write to real media.

If Retro68 lives elsewhere, point `RETRO68` at it:

```sh
RETRO68=/opt/Retro68/toolchain ARCH=ppc ./build.sh
```

## Which build runs where

| Build | Runs on | Notes |
|-------|---------|-------|
| `ARCH=68k` | 68000–68040 Macs (Plus … Quadra), and PowerPC Macs via the 68k emulator | the classic target |
| `ARCH=ppc` | Native PowerPC Macs (e.g. Power Mac G3) | faster; required for the statsd telemetry (Open Transport) |

## Optional: telemetry endpoints

Telemetry and debug logging ship **off** and are armed at runtime by a marker
file next to the app (see [`TELEMETRY.md`](TELEMETRY.md)). To point them at your
own collector, copy the template before building:

```sh
cp mac-cpp-source/MetricsConfig.local.h.example mac-cpp-source/MetricsConfig.local.h
$EDITOR mac-cpp-source/MetricsConfig.local.h    # set STATSD_HOST / LOG_HOST
```

`MetricsConfig.local.h` is git-ignored; without it the localhost defaults apply.

## Build-time feature flags

Compile-time behaviour is controlled by `#define`s in
`mac-cpp-source/VNCConfig.h` and `VNCServer.h` (feature toggles, and optional
hard-coded screen geometry/depth for a smaller/faster single-target binary).
See the *Build-time configuration* notes in the repo for details.

**Color depth:** generic builds accept indexed depths (1/2/4/8 bpp) and native
true color (16/32 bpp). Optional compile-time depth macros include
`VNC_FB_256_COLORS`, `VNC_FB_16BIT`, and `VNC_FB_32BIT`.

## CodeWarrior

The original project file (`mac-cpp-source/MiniVNC.µ.bin`, creator `CWIE`) and
resources (`MiniVNC.µ.rsrc.bin`) are still in the tree. Open the project in
CodeWarrior 8 (or Symantec C++ 7) on a real Mac or under Basilisk II to build
the classic way.
