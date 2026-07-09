#include "DebugLog.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define CAPACITY 2048

/* msgbuf.cpp allows you to defer a printf from an interrupt handler to a later date in the
   main event loop, where the memory manager won't hate you.

   Call dprintf() in your interrupt routine, then do_deferred_output in your event loop. */

char buffer[CAPACITY];
int wrpos = 0;
int rdpos = 0;

// Prints a debugging error message. If the message begins with "-" it will be shown
// on the main VNC user interface.

void _dprintf(const char* format, ...) {
    char str[256];

    // Do the printf
    va_list args;
    va_start(args, format);
    const int len = vsprintf(str, format, args);
    if (len > 255) {
        // Abort if we overflow the buffer, shame CodeWarrior does not have svnprintf!
        abort();
    }
    va_end(args);

    // Copy characters to ring buffer
    for(int i = 0; i <= len; i++) {
        buffer[wrpos] = str[i];
        wrpos = (wrpos+1) % CAPACITY;
    }
}

#if !USE_STDOUT
// Log sink for the Retro68 build (no SIOUX console). Default: discard. This is
// the single seam to redirect debug output to — e.g. a UDP mirror (loglisten.py,
// casquinha-style) once Open Transport / MacTCP is up.
void logSink(const char *str) { (void)str; }
#endif

#if defined(__ppc__)
// Retro68 leaves _consolewrite/_consoleread as overridable imports (linker uses
// -undefined=_consolewrite). If the app defines neither, they stay UNRESOLVED and
// the PowerPC CFM fragment FAILS TO PREPARE -> the app never launches. Providing
// them here resolves the import and, as a bonus, routes any stray stdio through
// the log sink. (68k links the runtime's default stub, so it's PPC-only here.)
#include <sys/types.h>
extern "C" ssize_t _consolewrite(int fd, const void *buf, size_t count) {
    (void)fd;
    char line[256];
    size_t n = count < 255 ? count : 255;
    for (size_t i = 0; i < n; i++) line[i] = ((const char*)buf)[i];
    line[n] = '\0';
    logSink(line);
    return (ssize_t)count;
}
extern "C" ssize_t _consoleread(int fd, void *buf, size_t count) {
    (void)fd; (void)buf; (void)count;
    return 0;
}
#endif

void _do_deferred_output() {
    char str[256];
    unsigned char len = 0;
    while (rdpos != wrpos) {
        // Pull character from ring buffer
        unsigned char c = buffer[rdpos];
        rdpos = (rdpos+1) % CAPACITY;

        // Output the string
        str[len++] = c;
        if (c == '\0') {
        #if USE_STDOUT
            // SIOUX console (CodeWarrior). Pulling stdout/fputs on Retro68 imports
            // RetroConsole (_consolewrite), which is unresolved unless the console
            // library is linked -> the CFM app fails to launch. So it's compiled
            // out here; the log sink for the Retro68 build is wired separately.
            fputs(str, stdout);
        #else
            logSink(str);
        #endif
            len = 0;
        }
    }
}