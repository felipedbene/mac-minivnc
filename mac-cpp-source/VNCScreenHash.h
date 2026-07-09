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

#pragma once

#include "VNCTypes.h"
#include <Retrace.h>

#define requestAlreadyScheduled -999

// GCC's m68k backend ICEs on the `pascal` attribute applied to a static member
// function. Our A5-trampoline (preVBLTask) is hand-written asm under GCC and pops
// the callee's argument itself, so the VBL member can safely use C convention there.
#if defined(__GNUC__)
    #define VBL_PASCAL
#else
    #define VBL_PASCAL pascal
#endif

typedef pascal void (*HashCallbackPtr)(int x, int y, int w, int h);

class VNCScreenHash {
    private:
        static VBL_PASCAL void myVBLTask(VBLTaskPtr theVBL);
        static VBL_PASCAL void preVBLTask();
        static OSErr makeVBLTaskPersistent(VBLTaskPtr task);
        static OSErr disposePersistentVBLTask(VBLTaskPtr task);

        static OSErr initVBLTask();
        static OSErr destroyVBLTask();
        static OSErr runVBLTask();

        static void beginCompute();
        static void computeHashes(unsigned int rows);
        static void computeHashesFast(unsigned int rows);
        static void computeHashesFastest(unsigned int rows);
        static void computeDirty(int &x, int &y, int &w, int &h);
        static void endCompute();
    public:
        static OSErr setup();
        static OSErr destroy();
        static OSErr requestDirtyRect(HashCallbackPtr);
};