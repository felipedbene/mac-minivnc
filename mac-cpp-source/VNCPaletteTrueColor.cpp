/****************************************************************************
 *   MiniVNC (c) 2022-2024 Marcio Teixeira                                  *
 *                                                                          *
 *   This program is free software: you can redistribute it and/or modify   *
 *   it under the terms of the GNU General Public License as published by   *
 *   the Free Software Foundation, either version 3 of the License, or      *
 *   (at your option) any later version.                                    *
 *                                                                          *
 *   This program is distributed in the hope that it will be useful,        *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *   GNU General Public License for more details.                           *
 *                                                                          *
 *   To view a copy of the GNU General Public License, go to the following  *
 *   location: <http://www.gnu.org/licenses/>.                              *
 ****************************************************************************/

#include "DebugLog.h"
#include "GestaltUtils.h"

#include "VNCConfig.h"
#include "VNCServer.h"
#include "VNCPalette.h"
#include "VNCEncoder.h"
#include "VNCFrameBuffer.h"

#include "VNCTypes.h"

static unsigned char pixelShift;
static unsigned long pixelMask;

unsigned long ctSeed;
extern unsigned long *vncTrueColors;

extern VNCPixelFormat pendingPixFormat;

static void setTrueColor(unsigned int i, int red, int green, int blue) {
    const unsigned long r = ((unsigned long)red)   * fbPixFormat.redMax   / 0xFFFF;
    const unsigned long g = ((unsigned long)green) * fbPixFormat.greenMax / 0xFFFF;
    const unsigned long b = ((unsigned long)blue)  * fbPixFormat.blueMax  / 0xFFFF;
    const unsigned long color = (r << fbPixFormat.redShift) | (g << fbPixFormat.greenShift) | (b << fbPixFormat.blueShift);
    if(fbPixFormat.bigEndian) {
        vncTrueColors[i] = color;
    } else {
        vncTrueColors[i] = ((color & 0x000000ff) << 24u) |
                           ((color & 0x0000ff00) << 8u)  |
                           ((color & 0x00ff0000) >> 8u)  |
                           ((color & 0xff000000) >> 24u);
    }
}

void VNCPalette::fillScreenPixelFormat(VNCPixelFormat &format, unsigned long depth) {
    format.bigEndian = 1;
    if (FB_IS_TRUECOLOR(depth)) {
        // Advertise 32-bit true color on the wire. Most VNC clients (including
        // RealVNC Viewer) do not render 16-bit server formats; native 16-bit
        // Mac framebuffers are expanded to 32-bit when encoding updates.
        format.trueColor = 1;
        format.bitsPerPixel = 32;
        format.depth = 24;
        format.redMax = 255;
        format.greenMax = 255;
        format.blueMax = 255;
        format.redShift = 16;
        format.greenShift = 8;
        format.blueShift = 0;
    } else {
        format.trueColor = 0;
        format.bitsPerPixel = 8;
        format.depth = depth;
        format.redMax = 3;
        format.greenMax = 7;
        format.blueMax = 3;
        format.redShift = 5;
        format.greenShift = 2;
        format.blueShift = 0;
    }
}

Boolean VNCPalette::pixelFormatsMatch(const VNCPixelFormat &a, const VNCPixelFormat &b) {
    return a.bitsPerPixel == b.bitsPerPixel &&
           a.depth == b.depth &&
           a.bigEndian == b.bigEndian &&
           a.trueColor == b.trueColor &&
           a.redMax == b.redMax &&
           a.greenMax == b.greenMax &&
           a.blueMax == b.blueMax &&
           a.redShift == b.redShift &&
           a.greenShift == b.greenShift &&
           a.blueShift == b.blueShift;
}

unsigned long VNCPalette::wireRowBytes(unsigned int pixels) {
    return pixels * fbPixFormat.bitsPerPixel / 8;
}

unsigned long VNCPalette::nativeRowBytes(unsigned int pixels) {
    #ifdef VNC_FB_BITS_PER_PIX
        const unsigned long depth = VNC_FB_BITS_PER_PIX;
    #else
        const unsigned long depth = fbDepth;
    #endif
    return pixels * depth / 8;
}

static unsigned long expand555To888(unsigned short pix) {
    const unsigned long r = ((pix >> 10) & 0x1F) * 255 / 31;
    const unsigned long g = ((pix >> 5) & 0x1F) * 255 / 31;
    const unsigned long b = (pix & 0x1F) * 255 / 31;
    return (r << 16) | (g << 8) | b;
}

void VNCPalette::copyNativeRowToWire(const unsigned char *src, unsigned char *dst, unsigned int pixels) {
    #ifdef VNC_FB_BITS_PER_PIX
        const unsigned long depth = VNC_FB_BITS_PER_PIX;
    #else
        const unsigned long depth = fbDepth;
    #endif
    if (depth == 16 && fbPixFormat.bitsPerPixel == 32) {
        const unsigned short *s = (const unsigned short*)src;
        unsigned long *d = (unsigned long*)dst;
        for (unsigned int i = 0; i < pixels; i++) {
            *d++ = expand555To888(*s++);
        }
    } else {
        BlockMove(src, dst, wireRowBytes(pixels));
    }
}

void VNCPalette::copyNativeTileToWire(const unsigned char *src, unsigned char *dst, short rows, short cols) {
    #ifdef VNC_BYTES_PER_LINE
        const unsigned long nativeStride = VNC_BYTES_PER_LINE;
    #else
        const unsigned long nativeStride = fbStride;
    #endif
    for (short row = 0; row < rows; row++) {
        copyNativeRowToWire(src, dst, cols);
        src += nativeStride;
        dst += wireRowBytes(cols);
    }
}

void VNCPalette::checkColorTable() {
    // Find the color table associated with the device
    GDHandle gdh = GetMainDevice();
    PixMapHandle gpx = (*gdh)->gdPMap;
    CTabHandle gct = (*gpx)->pmTable;

    // If the color table has changed, inform the interrupt routine
    if(gct && (*gct)->ctSeed != ctSeed) {
        vncFlags.fbColorMapNeedsUpdate = true;
    }
}

OSErr VNCPalette::updateColorTable() {
    #if !defined(VNC_FB_MONOCHROME)
        #ifdef VNC_FB_BITS_PER_PIX
            const unsigned long depth = VNC_FB_BITS_PER_PIX;
        #else
            const unsigned long depth = fbDepth;
        #endif

        // Native true color: no Mac color table to sync
        if (FB_IS_TRUECOLOR(depth)) {
            if (pendingPixFormat.bitsPerPixel) {
                if (!pixelFormatsMatch(pendingPixFormat, fbPixFormat)) {
                    BlockMove(&pendingPixFormat, &fbPixFormat, sizeof(VNCPixelFormat));
                    bytesPerColor = fbPixFormat.bitsPerPixel / 8;
                    prepareTrueColorRoutines(true);
                    dprintf("Changed pixel format.\n");
                }
                pendingPixFormat.bitsPerPixel = 0;
            }
            return noErr;
        }

        // Handle any changes to bits per pixel

        if (pendingPixFormat.bitsPerPixel) {
            BlockMove(&pendingPixFormat, &fbPixFormat, sizeof(VNCPixelFormat));
            pendingPixFormat.bitsPerPixel = 0;
            dprintf("Changed pixel format.\n");
            vncFlags.fbColorMapNeedsUpdate = true;
        }

        // Handle any changes to the color palette

        if (vncFlags.fbColorMapNeedsUpdate) {
            const unsigned int nColors = 1 << depth;

            // Allocate the color table, if necessary

            if (fbPixFormat.trueColor && (vncTrueColors == NULL)) {
                const unsigned long size = nColors * sizeof(unsigned long);
                vncTrueColors = (unsigned long *) NewPtr(size);
                if (MemError() != noErr) {
                    dprintf("Failed to allocate true color table\n", size);
                    return MemError();
                }
                dprintf("Reserved %ld bytes for true color table\n", size);
            }

            // Find the color table associated with the device
            GDHandle gdh = GetMainDevice();
            PixMapHandle gpx = (*gdh)->gdPMap;
            CTabHandle gct = (*gpx)->pmTable;
            if(gct) {
                if(nColors == ((*gct)->ctSize + 1)) {
                    // Store a copy of the indexed color table so that
                    // the interrupt routine can find it
                    for(unsigned int i = 0; i < nColors; i++) {
                        const RGBColor &rgb = (*gct)->ctTable[i].rgb;
                        if (fbPixFormat.trueColor) {
                            setTrueColor(i, rgb.red, rgb.green, rgb.blue);
                        } else {
                            setIndexedColor(i, rgb.red, rgb.green, rgb.blue);
                        }
                    }
                    ctSeed = (*gct)->ctSeed;
                    // Grab the white and black indices
                    GrafPtr savedPort;
                    GetPort (&savedPort);
                    CGrafPort cPort;
                    OpenCPort(&cPort);
                    VNCPalette::black = cPort.fgColor;
                    VNCPalette::white = cPort.bkColor;
                    CloseCPort(&cPort);
                    SetPort(savedPort);

                    dprintf("Color palette ready (size:%d b:%d w:%d)\n", nColors, VNCPalette::black, VNCPalette::white);
                } else {
                    dprintf("Palette size mismatch!\n");
                }
            } else {
                dprintf("Failed to get graphics device!\n");
            }
        } // vncFlags.fbColorMapNeedsUpdate
    #endif
    return noErr;
}

void VNCPalette::prepareTrueColorRoutines(Boolean isCPIXEL) {
    if (isCPIXEL) {
        // Determine representation of CPIXEL
        const unsigned long colorBits = ((unsigned long)fbPixFormat.redMax   << fbPixFormat.redShift) |
                                        ((unsigned long)fbPixFormat.greenMax << fbPixFormat.greenShift) |
                                        ((unsigned long)fbPixFormat.blueMax  << fbPixFormat.blueShift);
        if((fbPixFormat.bitsPerPixel == 32) && (colorBits == 0x00FFFFFF)) {
            bytesPerColor = 3;
        }
        //dprintf("Bytes per CPIXEL %d (ColorBits: %lx)\n", bytesPerColor, colorBits);
    }
    pixelShift  = (sizeof(unsigned long) - bytesPerColor)  * 8;
    pixelMask   = (1UL <<  pixelShift) - 1 ;
    if (!fbPixFormat.bigEndian) {
        pixelShift  = 0;
    }
}

#pragma a6frames off
#pragma optimize_for_size off
#pragma code68020 on

unsigned char *VNCPalette::emitTrueColor(unsigned char *dst, unsigned char color) {
    const unsigned long packed = vncTrueColors[color] << pixelShift;
    *(unsigned long *)dst = (((*(unsigned long *)dst) ^ packed) & pixelMask) ^ packed;
    return dst + bytesPerColor;
}

// Convert a native true-color tile into a stream of RFB CPIXELs (bytesPerColor
// bytes each), for use by the TRLE/ZRLE raw-tile type. Unlike copyNativeTileToWire
// (which emits full bitsPerPixel/8-byte RAW pixels), this emits the compressed
// CPIXEL form using the same pack/mask/shift convention as emitTrueColor, so the
// byte order matches the rest of the true-color encoder exactly.
// NOTE: the final *(unsigned long*) store writes up to 4 bytes; callers must give
// dst at least (rows*cols*bytesPerColor + 4) bytes of slack.
void VNCPalette::copyNativeTileToCPIXEL(const unsigned char *src, unsigned char *dst, short rows, short cols) {
    #ifdef VNC_FB_BITS_PER_PIX
        const unsigned long depth = VNC_FB_BITS_PER_PIX;
    #else
        const unsigned long depth = fbDepth;
    #endif
    #ifdef VNC_BYTES_PER_LINE
        const unsigned long nativeStride = VNC_BYTES_PER_LINE;
    #else
        const unsigned long nativeStride = fbStride;
    #endif
    const unsigned long cpixelRowBytes = (unsigned long)cols * bytesPerColor;
    for (short row = 0; row < rows; row++) {
        unsigned char *d = dst;
        if (depth == 16) {
            const unsigned short *s = (const unsigned short *)src;
            if (fbPixFormat.bitsPerPixel == 32) {
                // 16-bit screen, 32bpp/3-byte CPIXEL: expand 555 -> 888 first
                for (short col = 0; col < cols; col++) {
                    const unsigned long packed = expand555To888(*s++) << pixelShift;
                    *(unsigned long *)d = (((*(unsigned long *)d) ^ packed) & pixelMask) ^ packed;
                    d += bytesPerColor;
                }
            } else {
                for (short col = 0; col < cols; col++) {
                    const unsigned long packed = ((unsigned long)*s++) << pixelShift;
                    *(unsigned long *)d = (((*(unsigned long *)d) ^ packed) & pixelMask) ^ packed;
                    d += bytesPerColor;
                }
            }
        } else {
            const unsigned long *s = (const unsigned long *)src;
            for (short col = 0; col < cols; col++) {
                const unsigned long packed = (*s++) << pixelShift;
                *(unsigned long *)d = (((*(unsigned long *)d) ^ packed) & pixelMask) ^ packed;
                d += bytesPerColor;
            }
        }
        src += nativeStride;
        dst += cpixelRowBytes;
    }
}

#pragma optimize_for_size reset
#pragma a6frames reset
#pragma code68020 reset
