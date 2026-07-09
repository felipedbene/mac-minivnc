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

#include "statsd.h"

#include <stdarg.h>
#include <stdio.h>

/* Real implementation needs Open Transport (PowerPC / OS 9). Detect PPC across
 * toolchains (Retro68 defines __ppc__; CodeWarrior defines __POWERPC__/powerc).
 * ConditionalMacros.h #undefs __powerpc__, so that spelling is not used here. */
#if defined(__ppc__) || defined(__POWERPC__) || defined(powerc)
#define STATSD_OT 1
#else
#define STATSD_OT 0
#endif

/* ---- statsd_fmt: arch-independent line formatter -------------------------- */

int statsd_fmt(char *buf, int cap, const char *fmt, ...) {
    va_list ap;
    int n;
    if (!buf || cap <= 0) return 0;
    va_start(ap, fmt);
    n = vsnprintf(buf, (size_t)cap, fmt, ap);
    va_end(ap);
    if (n < 0)   n = 0;
    if (n > cap) n = cap;   /* vsnprintf returns the would-be length */
    return n;
}

#if STATSD_OT

#include <OpenTransport.h>
#include <OpenTransportProviders.h>   /* kUDPName, TUnitData */
#include <OpenTptInternet.h>          /* InetAddress, OTInitInetAddress, InetHost */

static EndpointRef g_ep    = kOTInvalidEndpointRef;
static InetAddress g_to;
static int         g_state = 0;       /* 0 = closed, 1 = open, -1 = failed */
static long        g_ok = 0, g_fail = 0, g_lasterr = 0;

/* Parse "a.b.c.d" into an InetHost. Returns 0 on failure. (Numeric only — the
 * collector is a fixed LAN IP, so no DNR round trip is needed.) */
static int parse_dotted_quad(const char *s, InetHost *out) {
    unsigned long parts[4];
    int i;
    const char *p = s;
    if (!s) return 0;
    for (i = 0; i < 4; i++) {
        unsigned long v = 0;
        int digits = 0;
        while (*p >= '0' && *p <= '9') { v = v * 10 + (unsigned long)(*p - '0'); p++; digits++; }
        if (!digits || v > 255) return 0;
        parts[i] = v;
        if (i < 3) { if (*p != '.') return 0; p++; }
    }
    if (*p != '\0') return 0;
    *out = (InetHost)((parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3]);
    return 1;
}

OSErr statsd_open(const char *ip, unsigned short port) {
    OSStatus err;
    InetHost host;

    if (g_state == 1) return noErr;              /* already open */
    if (!parse_dotted_quad(ip, &host)) return paramErr;

    /* Open Transport must already be up (InitOpenTransport in main.cpp). */
    g_ep = OTOpenEndpoint(OTCreateConfiguration(kUDPName), 0, NULL, &err);
    if (err != kOTNoError || g_ep == kOTInvalidEndpointRef) {
        g_ep = kOTInvalidEndpointRef;
        g_state = -1;
        return err ? (OSErr)err : ioErr;
    }
    OTSetSynchronous(g_ep);
    OTSetNonBlocking(g_ep);
    /* Send-only: bind to any local port (NULL req/ret). If a future Universal
     * Interfaces build rejects NULL, fall back to TBind with qlen = 0. */
    if (OTBind(g_ep, NULL, NULL) != kOTNoError) {
        OTCloseProvider(g_ep);
        g_ep = kOTInvalidEndpointRef;
        g_state = -1;
        return ioErr;
    }
    OTInitInetAddress(&g_to, (InetPort)port, host);
    g_state = 1;
    return noErr;
}

void statsd_send(const char *buf, int len) {
    TUnitData ud;
    OTResult  r;

    if (g_state != 1 || !buf || len <= 0) return;

    OTMemzero(&ud, sizeof(ud));
    ud.addr.buf  = (UInt8 *)&g_to;
    ud.addr.len  = sizeof(g_to);
    ud.udata.buf = (UInt8 *)buf;
    ud.udata.len = (ByteCount)len;

    r = OTSndUData(g_ep, &ud);
    if (r == kOTLookErr) {
        /* The OT UDP trap: one ICMP port-unreachable (collector not up yet)
         * queues a T_UDERR and every later send returns kOTLookErr — the
         * "exactly one datagram ever arrives" symptom. Clear it and retry. */
        if (OTLook(g_ep) == T_UDERR) OTRcvUDErr(g_ep, NULL);
        r = OTSndUData(g_ep, &ud);
    }
    if (r == kOTNoError) g_ok++;
    else { g_fail++; g_lasterr = (long)r; }
}

void statsd_close(void) {
    if (g_ep != kOTInvalidEndpointRef) {
        OTUnbind(g_ep);
        OTCloseProvider(g_ep);
        g_ep = kOTInvalidEndpointRef;
    }
    g_state = 0;
}

#else /* !STATSD_OT — no Open Transport on this target: no-op API */

OSErr statsd_open(const char *ip, unsigned short port) { (void)ip; (void)port; return 0; }
void  statsd_send(const char *buf, int len)            { (void)buf; (void)len; }
void  statsd_close(void)                               { }

#endif /* STATSD_OT */
