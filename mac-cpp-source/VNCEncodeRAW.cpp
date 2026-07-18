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

#include "VNCServer.h"
#include "VNCFrameBuffer.h"
#include "VNCEncodeRAW.h"
#include "VNCPalette.h"

int line;

Size VNCEncodeRaw::minBufferSize() {
    #ifdef VNC_FB_WIDTH
        const unsigned int width = VNC_FB_WIDTH;
    #else
        const unsigned int width = fbWidth;
    #endif
    return VNCPalette::wireRowBytes(width);
}

void VNCEncodeRaw::begin() {
    line = 0;
}

Boolean VNCEncodeRaw::getChunk(EncoderPB &epb) {
    const unsigned char *src = VNCFrameBuffer::getPixelAddr(fbUpdateRect.x, fbUpdateRect.y + line);
    const unsigned long rowBytes = VNCPalette::wireRowBytes(fbUpdateRect.w);
    VNCPalette::copyNativeRowToWire(src, epb.dst, fbUpdateRect.w);
    epb.bytesWritten = rowBytes;
    return ++line < fbUpdateRect.h;
}
