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
#include "VNCScreenHash.h"

#include "DebugLog.h"

#ifdef VNC_BYTES_PER_LINE
    #define COL_HASH_SIZE ((VNC_BYTES_PER_LINE + sizeof(unsigned long) - 1)/sizeof(unsigned long))
    #define ROW_HASH_SIZE (VNC_FB_HEIGHT)
#else
    #define COL_HASH_SIZE ((fbStride + sizeof(unsigned long) - 1)/sizeof(unsigned long))
    #define ROW_HASH_SIZE fbHeight;
#endif

//#define TEST_HASH

// Named VNCVBLProc (not VBLProcPtr) to avoid colliding with Retrace.h, which
// defines VBLProcPtr as a register-based UPP. This is the stack-based `pascal`
// routine our A5-trampoline (preVBLTask) invokes.
typedef VBL_PASCAL void (*VNCVBLProc)(VBLTaskPtr recPtr);

#if defined(__GNUC__) && !defined(__ppc__)
extern "C" void preVBLTaskAsm(); // 68k A5-trampoline, defined in top-level asm below
#endif

struct ExtendedVBLTaskRec {
    unsigned long ourA5; // Application A5
    VNCVBLProc ourProc;  // Address of VBL routine written in a high-level language
    VBLTask    vblTask;  // VBLTask record
} evbl;

struct MonoHashData {
    unsigned long     *rowHashPrev;
    unsigned long     *rowHashNext;
    unsigned long     *colHashPrev;
    unsigned long     *colHashNext;
};

static int row = 0;

static VNCRect dirtyRect;
static HashCallbackPtr callback;

static MonoHashData *data = NULL;

// Prototypes

void unionRect(const VNCRect *a,VNCRect *b);

#define ALIGN_PAD 3
#define ALIGN_LONG(PTR) (PTR) + (sizeof(unsigned long) - (unsigned long)(PTR) % sizeof(unsigned long))

OSErr VNCScreenHash::setup() {
    const size_t colHashSize = COL_HASH_SIZE;
    const size_t rowHashSize = ROW_HASH_SIZE;
    const size_t dataSize = sizeof(MonoHashData) + (colHashSize + rowHashSize) * 2 * sizeof(unsigned long) + ALIGN_PAD;
    data = (MonoHashData*) NewPtr(dataSize);
    if (MemError() != noErr)
        return MemError();
    dprintf("Reserved %ld bytes for dirty rect detection\n", dataSize);

    Ptr hashPtr = ALIGN_LONG((Ptr)data + sizeof(MonoHashData));

    #if USE_SANITY_CHECKS
        if (((unsigned long)hashPtr % 4) != 0) {
            dprintf("Screen hash not long word aligned %ld\n", (unsigned long)hashPtr % 4);
        }
    #endif

    data->rowHashPrev = (unsigned long*)hashPtr;
    data->rowHashNext = data->rowHashPrev + rowHashSize;
    data->colHashPrev = data->rowHashNext + rowHashSize;
    data->colHashNext = data->colHashPrev + colHashSize;

    // Setup the VBL task record
    evbl.ourA5 = SetCurrentA5();
    evbl.ourProc = myVBLTask;
    evbl.vblTask.qType = vType;
#if defined(__ppc__)
    // PowerPC: wrap the VBL routine in a UPP (Mixed Mode restores our RTOC when
    // fired). CRUCIAL: allocate the RoutineDescriptor in the SYSTEM heap. The
    // Vertical Retrace Manager only keeps servicing an application's VBL task
    // while that app is in the background if the task's entry point lives in
    // globally-valid memory — this is the PowerPC analogue of the 68k system-heap
    // JMP stub in makeVBLTaskPersistent(). With the UPP in the app heap the task
    // silently stops being called the moment MiniVNC is switched out.
    {
        THz saveZone = GetZone();
        SetZone(SystemZone());
        evbl.vblTask.vblAddr = NewVBLUPP(myVBLTask);
        SetZone(saveZone);
    }
#elif defined(__GNUC__)
    evbl.vblTask.vblAddr = (VBLUPP)preVBLTaskAsm;
#else
    evbl.vblTask.vblAddr = preVBLTask;
#endif
    evbl.vblTask.vblCount = 0;
    evbl.vblTask.vblPhase = 0;

    dirtyRect.x = 0;
    dirtyRect.y = 0;
    dirtyRect.w = 0;
    dirtyRect.h = 0;

    callback = 0;

    OSErr err = makeVBLTaskPersistent(&evbl.vblTask);

    // Compute the first checksum
    requestDirtyRect(0);
    return err;
}

OSErr VNCScreenHash::destroy() {
    DisposPtr((Ptr)data);
    data = NULL;

    VRemove((QElemPtr)&evbl.vblTask);
    callback = NULL;
    return disposePersistentVBLTask(&evbl.vblTask);
}

void unionRect(const VNCRect *a,VNCRect *b) {
    if(b->w && b->h) {
        if(a->w && a->h) {
            const int ax2 = a->x + a->w, ay2 = a->y + a->h;
            const int bx2 = b->x + b->w, by2 = b->y + b->h;
            const int cx1 = min(a->x,b->x);
            const int cy1 = min(a->y,b->y);
            const int cx2 = max(ax2,bx2);
            const int cy2 = max(ay2,by2);
            b->x = cx1;
            b->y = cy1;
            b->w = cx2 - cx1;
            b->h = cy2 - cy1;
        }
    } else {
        *b = *a;
    }
}

/************************** VBL TASK ************************/

// From Inside Macintosh: Process page 4-20, Using the Vertical Retrace Manager
OSErr VNCScreenHash::makeVBLTaskPersistent(VBLTaskPtr task) {
#if defined(__ppc__)
    // PowerPC: vblAddr is already a NewVBLUPP RoutineDescriptor and CFM code is
    // stable while the app runs, so no persistence stub is needed. The 68k JMP
    // trampoline below would make the Vertical Retrace Manager execute the
    // RoutineDescriptor as 68k code on the first retrace -> type 10 crash.
    (void)task;
    return noErr;
#else
    struct JMPInstr {
        unsigned short  opcode;
        void            *address;
    } *sysHeapPtr;

    // get a block in the system heap
    sysHeapPtr = (JMPInstr*) NewPtrSys(sizeof(JMPInstr));
    OSErr err = MemError();
    if(err != noErr) return err;

    // populate the instruction
    sysHeapPtr->opcode  = 0x4EF9;       // this is an absolute JMP
    sysHeapPtr->address = task->vblAddr; // this is the JMP address

    task->vblAddr = (VBLUPP) sysHeapPtr;
    return noErr;
#endif
}

OSErr VNCScreenHash::disposePersistentVBLTask(VBLTaskPtr task) {
#if defined(__ppc__)
    // On PPC vblAddr is the NewVBLUPP RoutineDescriptor (no 68k stub was made).
    if (task->vblAddr) DisposeVBLUPP((VBLUPP)task->vblAddr);
    task->vblAddr = 0;
    return noErr;
#else
    DisposPtr((Ptr)task->vblAddr);
    task->vblAddr = 0;
    return MemError();
#endif
}

#if defined(__ppc__)
// PowerPC: the VBL routine is wrapped in a UPP (NewVBLUPP) at install time and the
// Mixed Mode Manager handles the environment, so no A5-trampoline is emitted here.
#elif defined(__GNUC__)
// Retro68/GCC translation of the CodeWarrior A5-trampoline below, emitted as
// top-level asm so GCC adds no prologue/epilogue. The OS enters with A0 = &vblTask;
// the app's A5 sits at -8(A0) and the real routine (ourProc) at -4(A0). The real
// routine is C-convention under GCC, so the trampoline caller-pops the arg (addq).
extern "C" void preVBLTaskAsm();
__asm__(
"    .text\n"
"    .globl preVBLTaskAsm\n"
"preVBLTaskAsm:\n"
"    link %a6,#0\n"
"    movem.l %a5,-(%sp)\n"
"    move.l %a0,-(%sp)\n"
"    move.l -8(%a0),%a5\n"
"    move.l -4(%a0),%a0\n"
"    jsr (%a0)\n"
"    addq.l #4,%sp\n"          // caller-pops: myVBLTask is C-convention under GCC
"    movem.l (%sp)+,%a5\n"
"    unlk %a6\n"
"    rts\n"
);
#else
asm pascal void VNCScreenHash::preVBLTask() {
    link    a6,#0                // Link for the debugger
    movem.l a5,-(sp)             // Preserve the A5 register
    move.l  a0,-(sp)             // Pass PB pointer as the parameter
    move.l  -8(a0),a5            // Set A5 to passed value (ourA5).
    move.l  -4(a0),a0            // A0 = real completion routine address
    jsr     (a0)                 // Transfer control to ourCompletion
    movem.l (sp)+,a5             // Restore A5 register
    unlk    a6                   // Unlink.
    rts                          // Return
    dc.b    0x8A,"PreVBLTask"
    dc.w    0x0000
}
#endif // __GNUC__

OSErr VNCScreenHash::requestDirtyRect(HashCallbackPtr func) {
    if(callback == NULL) {
        evbl.vblTask.vblCount = 1;

        callback = func;
        row = 0;
        beginCompute();

        return VInstall((QElemPtr)&evbl.vblTask);
    }
    return requestAlreadyScheduled;
}

VBL_PASCAL void VNCScreenHash::myVBLTask(VBLTaskPtr theVBL) {
    #if defined(TEST_HASH)
        if(1) {
            beginCompute();
            computeHashesFast(VNC_FB_HEIGHT);
            endCompute();
            beginCompute();
            for(int i = 0; i < (VNC_BYTES_PER_LINE / 16); i++) {
                computeHashesFastest(i);
            }
            endCompute();
            short colChk = memcmp(data->colHashPrev, data->colHashNext, 16);
            short rowChk = memcmp(data->rowHashPrev, data->rowHashNext, ROW_HASH_SIZE);
            dprintf("Col: %s Row: %s ", colChk ? "ne" : "eq", rowChk ? "ne" : "eq");
            callback(0, 0, VNC_FB_WIDTH, VNC_FB_HEIGHT);
            callback = NULL;
        }
    #else
        #ifdef VNC_FB_HEIGHT
            const unsigned int fbHeight = VNC_FB_HEIGHT;
        #endif
        if(row < fbHeight) {
            // Rows hashed per VBL tick. The 68000 splits the pass across 16
            // ticks so a single interrupt doesn't hog the CPU; PowerPC is far
            // faster, so it uses bigger chunks (fewer ticks per pass) to cut
            // update latency, while still doing less per tick than a 68k tick.
            #if defined(__ppc__)
                const unsigned int rowChunk = fbHeight / 4;
            #else
                const unsigned int rowChunk = fbHeight / 16;
            #endif
            const unsigned int numRows = min(fbHeight - row, rowChunk);
            computeHashesFast(numRows);
            row += numRows;
            theVBL->vblCount = 1;
        }
    #endif
        else  {
            endCompute();
            VNCRect newDirt;
            // computeDirty takes int& but VNCRect fields are unsigned short; bridge
            // through int temporaries (CodeWarrior bound the mismatched refs directly).
            int ndx, ndy, ndw, ndh;
            computeDirty(ndx, ndy, ndw, ndh);
            newDirt.x = ndx; newDirt.y = ndy; newDirt.w = ndw; newDirt.h = ndh;

            Boolean gotOldDirt = dirtyRect.w && dirtyRect.h;
            Boolean gotNewDirt = newDirt.w && newDirt.h;

            // Merge the two rectangles
            unionRect(&newDirt, &dirtyRect);

            if(gotOldDirt) {
                // Update and forfeit the rect
                if(callback) {
                    callback(dirtyRect.x, dirtyRect.y, dirtyRect.w, dirtyRect.h);
                    callback = NULL;
                }
                dirtyRect.x = 0;
                dirtyRect.y = 0;
                dirtyRect.w = 0;
                dirtyRect.h = 0;
            } else {
                // Not enough dirt, so keep waiting. Shorter poll wait on the
                // faster PowerPC to cut update latency (68k keeps its 16 ticks).
                row = 0;
                beginCompute();
                #if defined(__ppc__)
                    theVBL->vblCount = 4;
                #else
                    theVBL->vblCount = 16;
                #endif
            }
        }
}

/************************** HASHING ************************/

static const unsigned long *scrnPtr;
static unsigned long *scrnRowHashPtr;
static unsigned long *scrnColHashPtr;

void VNCScreenHash::beginCompute() {
    scrnPtr = (unsigned long*) VNCFrameBuffer::getBaseAddr();
    scrnRowHashPtr = data->rowHashNext;
    scrnColHashPtr = data->colHashNext;

    // Clear the next column buffer
    ZERO_ANY (unsigned long, data->colHashNext, COL_HASH_SIZE);
}

void VNCScreenHash::computeDirty(int &x, int &y, int &w, int &h) {
    const size_t colHashSize = COL_HASH_SIZE;
    const size_t rowHashSize = ROW_HASH_SIZE;
    #ifdef VNC_FB_BITS_PER_PIX
        const unsigned char pixPerByte = 8 / VNC_FB_BITS_PER_PIX;
    #else
        const unsigned char pixPerByte = 8 / fbDepth;
    #endif
    unsigned int x1 = 0;
    unsigned int y1 = 0;
    while((x1 < colHashSize) && (data->colHashNext[x1] == data->colHashPrev[x1])) x1++;
    while((y1 < rowHashSize) && (data->rowHashNext[y1] == data->rowHashPrev[y1])) y1++;

    unsigned int x2 = colHashSize-1;
    unsigned int y2 = rowHashSize-1;
    while((x2 > x1) && (data->colHashNext[x2] == data->colHashPrev[x2])) x2--;
    while((y2 > y1) && (data->rowHashNext[y2] == data->rowHashPrev[y2])) y2--;
    x2++;
    y2++;
    x1 *= 4 * pixPerByte;
    x2 *= 4 * pixPerByte;

    if(x2 > x1 && y2 > y1) {
        x = x1;
        y = y1;
        w = x2 - x1;
        h = y2 - y1;
    } else {
        x = 0;
        y = 0;
        w = 0;
        h = 0;
    }
}

// Prepare the hashes so that we can start processing a new screen

void VNCScreenHash::endCompute() {
    // Swap the prev and next buffers
    unsigned long *tmp;
    #define SWAP(A,B) tmp = A; A = B; B = tmp;
    SWAP(data->colHashPrev, data->colHashNext);
    SWAP(data->rowHashPrev, data->rowHashNext);
}

/* This is the C++ implementation of computeHashes(). It was optimized
 * by looking at the disassembly and using temporary variables to try
 * to force the compiler to use register variables inside the loop.
 */
void VNCScreenHash::computeHashes(unsigned int rows) {
    const unsigned long *l = scrnPtr;

    // Process one full framebuffer row per iteration. COL_HASH_SIZE is
    // ceil(stride/4), so this covers the ENTIRE row width and advances `l` by
    // exactly one stride to the next row. (The previous version hard-coded 16
    // longs = 64 bytes, which was only correct for the original 512x1-bit mono
    // build. On an 800x8-bit / 832-stride screen it hashed just the leftmost 64
    // pixels and mis-stepped rows, so change detection only ever saw x=0..64.)
    const size_t colHashSize = COL_HASH_SIZE;

    //HideCursor();
    for(;rows--;) {
        unsigned long  rowHash = 0;
        unsigned long *colHash = data->colHashNext;
        for(size_t c = 0; c < colHashSize; c++) {
            const unsigned long pix = *l++;
            rowHash  += pix;
            colHash[c] += pix;
        }
        *scrnRowHashPtr++ = rowHash;
    }
    //ShowCursor();
    scrnPtr = l;
}

/* An optimized version of computeHashes() with half as many instruction
 * words, which is estimated to reduce total memory access by 25%. This
 * is done by using movem.l to read either 20 and 24 bytes at a time.
 */
#if defined(__GNUC__)
// Retro68/GCC: use the portable C computeHashes() in place of the asm
// optimization — identical semantics (row + column hashes, advances the globals).
void VNCScreenHash::computeHashesFast(unsigned int rows) { computeHashes(rows); }
#else
asm void VNCScreenHash::computeHashesFast(unsigned int rows) {
    /*
     * Register Assignments:
     *   A0                     : Source ptr
     *   A1                     : Col hash ptr
     *   A2                     : Row hash ptr
     *   A3                     : Line sum
     *   A4, A5                 : Unused
     *   A6                     : Link for debugger
     *   A7                     : Stack ptr
     *   D0                     : Line count from argument rows
     *   D1,D2,D3,D4,D5,D6      : Source pixels (up to 192 at a time)
     *   D7                     : Unused
     */

    link    a6,#0000           // Link for debugger
    movem.l d3-d6/a2-a3,-(a7)  // Save registers

    //_HideCursor

    move.w  8(a6),d0           // Load the line count

    movea.l scrnPtr,a0
    movea.l scrnRowHashPtr,a2

    bra sumLine
sumLoop:
    movea.l scrnColHashPtr, a1;// Set pointer to column hashes
    movea.l #0,a3              // Clear the line sum

    #if (VNC_FB_WIDTH == 512) && (VNC_FB_BITS_PER_PIX == 1)
        // Use the fewest overall instructions since we are on the 68000 w/o a instruction cache

        // Columns 1 through 160
        movem.l (a0)+,d1-d5        // Load 160 pixels
        adda.l  d1,a3              // Add to row sum
        adda.l  d2,a3
        adda.l  d3,a3
        adda.l  d4,a3
        adda.l  d5,a3
        add.l   d1,(a1)+           // Add to col sum
        add.l   d2,(a1)+
        add.l   d3,(a1)+
        add.l   d4,(a1)+
        add.l   d5,(a1)+

        // Columns 161 through 320
        movem.l (a0)+,d1-d5        // Load 160 pixels
        adda.l  d1,a3              // Add to row sum
        adda.l  d2,a3
        adda.l  d3,a3
        adda.l  d4,a3
        adda.l  d5,a3
        add.l   d1,(a1)+           // Add to col sum
        add.l   d2,(a1)+
        add.l   d3,(a1)+
        add.l   d4,(a1)+
        add.l   d5,(a1)+

        // Columns 321 through 512
        movem.l (a0)+,d1-d6        // Load 192 pixels
        adda.l  d1,a3              // Add to row sum
        adda.l  d2,a3
        adda.l  d3,a3
        adda.l  d4,a3
        adda.l  d5,a3
        adda.l  d6,a3
        add.l   d1,(a1)+           // Add to col sum
        add.l   d2,(a1)+
        add.l   d3,(a1)+
        add.l   d4,(a1)+
        add.l   d5,(a1)+
        add.l   d6,(a1)+
    #else
        // We might have differing pixel depths or a resolution of either 512 or 640,
        // so transfer 16 bytes at time as this divides cleanly into all possibilities.

        #ifndef VNC_BYTES_PER_LINE
            move.w fbStride,d5
            asr.w #4,d5
        #else
            move.w #VNC_BYTES_PER_LINE/16, d5
        #endif

        bra transferChunk
    chunkLoop:
        // Transfer 16 bytes at a time using four registers
        movem.l (a0)+,d1-d4        // Load 128 pixels
        adda.l  d1,a3              // Add to line sum
        adda.l  d2,a3
        adda.l  d3,a3
        adda.l  d4,a3
        add.l   d1,(a1)+           // Add to values to column hashes
        add.l   d2,(a1)+
        add.l   d3,(a1)+
        add.l   d4,(a1)+

    transferChunk:
        dbra d5, chunkLoop

        // Transfer remaining bytes that are not divisible by 16 bytes
        // in chunks of two bytes instead.

        #ifndef VNC_BYTES_PER_LINE
            move.w fbStride,d5
            and.w #15,d5
            asr.w #1,d5
        #else
            move.w #(VNC_BYTES_PER_LINE & 15) / 2, d5
        #endif

        bra transferWords
    wordLoop:
        // Transfer two bytes at a time using one register
        move.w (a0)+,d1            // Load 16 pixels
        adda.w  d1,a3              // Add to line sum
        add.w   d1,(a1)+           // Add to values to column hashes

    transferWords:
        dbra d5, wordLoop
    #endif

    move.l a3,(a2)+            // Write the line sum

sumLine:
    dbra d0, sumLoop

    move.l   a0, scrnPtr       // Save updated ptr
    move.l   a2, scrnRowHashPtr

    //_ShowCursor

noRows:
    movem.l (a7)+,d3-d6/a2-a3  // Restore registers
    unlk    a6
    rts                        // Return
}
#endif // __GNUC__ (computeHashesFast)

#if defined(__GNUC__)
// Only referenced by the TEST_HASH diagnostic path; computeHashes() already
// covers column hashes. Stubbed for the Retro68 build (TODO if TEST_HASH needed).
void VNCScreenHash::computeHashesFastest(unsigned int column) { (void)column; }
#else
asm void VNCScreenHash::computeHashesFastest(unsigned int column) {
    /*
     * Register Assignments:
     *   A0                      : Source ptr
     *   A1                      : Row hash ptr
     *   A5                      : Application globals
     *   A6                      : Link for debugger
     *   A7                      : Stack ptr
     *   D0                      : Starting column offset
     *   D1                      : Rows in screen
     *   D2,D3,D4,D5             : Source pixels (up to 128 at a time)
     *   A2,A3,A4,D6             : Column sums
     *   D7                      : Linestride
     */

    link    a6,#0000           // Link for debugger
    movem.l d3-d7/a2-a5,-(a7)  // Save registers

    //_HideCursor

    movea.l scrnPtr,a0
    movea.l scrnRowHashPtr,a1

    // Compute offset to starting column
    move.w  8(a6),d0           // Load the starting column
    lsl.w   #4,d0              // Multiply by 16
    adda.w  d0, a0             // Offset to starting column

    // Compute linestride and rows

    #ifndef VNC_BYTES_PER_LINE
        move.w fbStride,d7
        sub.w #16,d7
    #else
        move.w  #VNC_BYTES_PER_LINE-16,d7
    #endif
    #ifndef VNC_FB_HEIGHT
        move.w  fbHeight,d1
    #else
        move.w  #VNC_FB_HEIGHT,d1
    #endif

    clr.l   d2                 // Clear column sums
    movea.l d2, a2
    movea.l d2, a3
    movea.l d2, a4
    move.l  d2, d6

    bra nextRow
rowLoop:
    movem.l (a0)+,d2-d5        // Load 128 pixels
    adda.l  d7, a0             // Move to next row

    adda.l  d2, a2             // Add to column sums
    adda.l  d3, a3
    adda.l  d4, a4
    add.l   d5, d6

    add.l   d2, d3             // Add to row sum
    add.l   d3, d4
    add.l   d4, d5
    add.l   d5, (a1)+

nextRow:
    dbra d1, rowLoop           // Are we on the last row?

    movea.l scrnColHashPtr,a1  // Set pointer to column hashes
    adda.w  d0, a1             // Offset to starting column
    move.l  a2,(a1)+           // Write column sums
    move.l  a3,(a1)+
    move.l  a4,(a1)+
    move.l  d6,(a1)+

    //_ShowCursor

    movem.l (a7)+,d3-d7/a2-a5  // Restore registers
    unlk    a6
    rts                        // Return
}
#endif // __GNUC__ (computeHashesFastest)