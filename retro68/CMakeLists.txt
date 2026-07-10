cmake_minimum_required(VERSION 3.10)
project(MiniVNC CXX C)

# Cross-builds the classic-68k MiniVNC app with the Retro68 toolchain (GCC + Rez
# + MakeAPPL). Point CMAKE_TOOLCHAIN_FILE at:
#   $RETRO68/m68k-apple-macos/cmake/retro68.toolchain.cmake
# See build.sh in this directory.

set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/../mac-cpp-source)

# The application's signature/creator is 'µVNC' (µ = Mac Roman byte 0xB5). It
# MUST match the 'µVNC' signature resource + BNDL/FREF baked into
# MiniVNC.µ.rsrc.bin, or the Finder can't bind the app's icon family and shows
# the generic application icon. (The earlier port used "MnVN", which is why the
# built app lost its icon.) string(ASCII 181 ...) emits the raw 0xB5 byte so we
# don't put a UTF-8 'µ' — which would be two bytes — into this source file.
string(ASCII 181 MINIVNC_MU)
set(MINIVNC_CREATOR "${MINIVNC_MU}VNC")

add_application(MiniVNC
    CREATOR "${MINIVNC_CREATOR}"

    # --- core sources ---
    ${SRC}/main.cpp
    ${SRC}/VNCServer.cpp
    ${SRC}/VNCConfig.cpp
    ${SRC}/VNCFrameBuffer.cpp
    ${SRC}/VNCScreenHash.cpp
    ${SRC}/VNCStreamReader.cpp
    ${SRC}/ChainedTCPHelper.cpp
    ${SRC}/DebugLog.cpp
    ${SRC}/DialogUtils_VarArgs.cpp
    ${SRC}/TightVNCSupport.cpp
    ${SRC}/TightVNCTypes.c
    ${SRC}/statsd.c                # fio A1: statsd-over-OT metrics (PPC; no-op on 68k)
    ${SRC}/metrics.c               # fio A3: 1 Hz metric accumulators + flush

    # --- palette / encoders ---
    ${SRC}/VNCPalette.cpp
    ${SRC}/VNCPaletteTrueColor.cpp
    ${SRC}/VNCEncoder.cpp
    ${SRC}/VNCEncodeCursor.cpp
    ${SRC}/VNCEncoderZLib.cpp
    ${SRC}/VNCEncodeRAW.cpp
    ${SRC}/VNCEncodeHextile.cpp
    ${SRC}/VNCEncodeTight.cpp
    ${SRC}/VNCEncodeTRLE.cpp
    ${SRC}/VNCEncodeTRLEMono.cpp
    ${SRC}/VNCEncodeZRLE.cpp
    ${SRC}/VNCEncodeTilesASM.cpp   # compiles empty under GCC (USE_ASM_CODE 0)
    ${SRC}/VNCEncodeTilesC.cpp

    # --- vendored libs ---
    ${SRC}/libs/miniz-3.0.2/miniz.c
    "${SRC}/libs/Ari Halberstadt/OSUtilities.cpp"
    "${SRC}/libs/Common Libs/GestaltUtils.cpp"
    ${SRC}/libs/ChromiVNC/VNCKeyboard.cpp

    # --- resources: reuse the existing ResEdit resource fork (MacBinary) ---
    ${SRC}/MiniVNC.µ.rsrc.bin
)

# libs/ root is intentionally omitted so "MacTCP.h" resolves to Retro68's
# universal (CFM-correct) header, not the vintage bundled libs/MacTCP.h.
target_include_directories(MiniVNC PRIVATE
    ${SRC}
    ${SRC}/libs/miniz-3.0.2
    "${SRC}/libs/Ari Halberstadt"
    "${SRC}/libs/Common Libs"
    ${SRC}/libs/ChromiVNC
)

# Reproduce CodeWarrior's MacHeaders prefix + old Toolbox names for every TU.
target_compile_definitions(MiniVNC PRIVATE TARGET_API_MAC_OS8=1 OLDROUTINENAMES=1)
target_compile_options(MiniVNC PRIVATE
    -include ${CMAKE_CURRENT_SOURCE_DIR}/prefix.h
    -ffunction-sections -fdata-sections
    # The code was written for 68k, where `char` is signed. PowerPC defaults to
    # unsigned char, which breaks `char` loop counters like `while (--n != -1)`.
    # Force signed char everywhere so PPC matches the 68k assumptions.
    -fsigned-char
    $<$<COMPILE_LANGUAGE:CXX>:-fpermissive;-fno-exceptions;-fno-rtti>
)
# Dead-code elimination (matches casquinha's known-good config): drops unused glue
# such as the RetroConsole stubs pulled in by stdio, and shrinks the binary.
target_link_options(MiniVNC PRIVATE "LINKER:-gc-sections")

# fio A1: the statsd module uses Open Transport, which exists only on the PowerPC
# (CFM) target — link the OT client libraries there only. On the 68k build statsd.c
# compiles to no-op stubs and needs no OT libs. (Build/test is manual on macg5; if
# that toolchain isn't Retro68, wire the equivalent OT libs in its project.)
if(CMAKE_CXX_COMPILER MATCHES "powerpc-apple-macos")
    target_link_libraries(MiniVNC
        OpenTransportAppPPC
        OpenTptInetPPC
        OpenTptInternetLib
        OpenTransportLib
    )
endif()
