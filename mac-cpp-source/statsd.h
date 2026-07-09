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
 * statsd — fire-and-forget statsd metrics over Open Transport UDP.
 *
 * A tiny, self-contained UDP emitter for shipping statsd-format metrics
 * ("name:value|type") to a collector. Modeled on casquinha's cq_tx_udp()
 * (src/cq_transport_ot.c): a single connectionless UDP endpoint, non-blocking,
 * that never blocks the caller and silently drops on flow control.
 *
 * Independent of MiniVNC's MacTCP VNC networking. Only does real work on a
 * PowerPC/OT build; on other targets every call is a no-op, so callers need
 * no #ifdefs. Open Transport must be initialized (InitOpenTransport) before
 * statsd_open() — see main.cpp lifecycle.
 */

#pragma once

#include <MacTypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Open the send-only UDP endpoint aimed at ip:port (ip is a dotted-quad
 * string, e.g. "10.0.100.116"). Returns noErr on success. Idempotent. */
OSErr statsd_open(const char *ip, unsigned short port);

/* Format one statsd line into buf (printf-style). Returns the byte length. */
int   statsd_fmt(char *buf, int cap, const char *fmt, ...);

/* Fire one datagram. Fire-and-forget: never blocks, drops on flow control,
 * clears the OT T_UDERR "one datagram only" trap and retries once. */
void  statsd_send(const char *buf, int len);

/* Register a second destination for plain-text log lines (a UDP log sink,
 * casquinha/loglisten style). Uses the same endpoint as statsd. */
OSErr statsd_log_open(const char *ip, unsigned short port);

/* Fire one plain-text log line to the log destination (fire-and-forget). */
void  statsd_log(const char *buf, int len);

/* Close the endpoint. */
void  statsd_close(void);

#ifdef __cplusplus
}
#endif
