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

#include "metrics.h"
#include "statsd.h"

#include <Events.h>   /* TickCount */

#define TICKS_PER_SEC 60          /* the classic Mac tick rate */

/* Per-second accumulators. Incremented (cheaply) from the hot path; read and
 * reset once per second in metrics_tick(). */
static unsigned long g_lastTick     = 0;
static int           g_boot_stage   = 0;
static long          g_sessions     = 0;   /* counter delta this second */
static unsigned long g_bytes        = 0;   /* counter delta this second */
static unsigned long g_frames       = 0;   /* -> fps gauge               */
static unsigned long g_frame_ms_sum = 0;   /* -> avg frame_ms            */
static unsigned long g_frame_ms_cnt = 0;

void metrics_boot_stage(int stage)  { g_boot_stage = stage; }
void metrics_session(void)          { g_sessions++; }
void metrics_bytes(unsigned long n) { g_bytes += n; }
void metrics_frame(unsigned long ms){ g_frames++; g_frame_ms_sum += ms; g_frame_ms_cnt++; }

void metrics_tick(void) {
    unsigned long now = TickCount();
    char buf[256];
    int len = 0;

    if (g_lastTick == 0) g_lastTick = now;
    if (now - g_lastTick < TICKS_PER_SEC) return;   /* not yet a second */
    g_lastTick = now;

    /* Batch every metric into ONE datagram (newline-separated statsd). */
    len += statsd_fmt(buf + len, (int)sizeof(buf) - len, "minivnc.up:1|c\n");
    len += statsd_fmt(buf + len, (int)sizeof(buf) - len, "minivnc.boot_stage:%d|g\n", g_boot_stage);
    len += statsd_fmt(buf + len, (int)sizeof(buf) - len, "minivnc.fps:%lu|g\n", g_frames);
    if (g_sessions)
        len += statsd_fmt(buf + len, (int)sizeof(buf) - len, "minivnc.sessions:%ld|c\n", g_sessions);
    if (g_bytes)
        len += statsd_fmt(buf + len, (int)sizeof(buf) - len, "minivnc.bytes:%lu|c\n", g_bytes);
    if (g_frame_ms_cnt)
        len += statsd_fmt(buf + len, (int)sizeof(buf) - len, "minivnc.frame_ms:%lu|ms\n",
                          g_frame_ms_sum / g_frame_ms_cnt);

    statsd_send(buf, len);

    /* Reset per-second deltas (boot_stage is sticky — it's a gauge). */
    g_sessions = 0;
    g_bytes = 0;
    g_frames = 0;
    g_frame_ms_sum = 0;
    g_frame_ms_cnt = 0;
}
