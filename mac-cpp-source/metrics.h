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

/*
 * metrics — 1 Hz statsd flush for MiniVNC.
 *
 * The hot path (framebuffer encode/send, completion routines) only ever
 * *increments longs* via the tiny inline-cost helpers below — no formatting,
 * no UDP. metrics_tick(), called from the main event loop, batches everything
 * into ONE statsd datagram at most once per second. 1 packet/s, not 30.
 *
 * All emission is main-loop time (statsd/OT is not interrupt-safe). Emission
 * points that feed these counters are wired in fio A4.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Startup-progress gauge (boot localization). Highest value the collector
 * receives = the last startup step reached before a crash. */
void metrics_boot_stage(int stage);

/* One client session established. */
void metrics_session(void);

/* n bytes sent to a client (hot path — accumulate only). */
void metrics_bytes(unsigned long n);

/* One framebuffer update encoded, taking ms milliseconds (hot path). */
void metrics_frame(unsigned long ms);

/* Call once per event-loop pass. Flushes the accumulated metrics as a single
 * datagram at ~1 Hz (gated on TickCount) and resets the per-second counters. */
void metrics_tick(void);

#ifdef __cplusplus
}
#endif
