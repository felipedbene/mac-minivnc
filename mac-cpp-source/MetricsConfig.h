/****************************************************************************
 *   MiniVNC (c) 2022-2024 Marcio Teixeira                                  *
 *                                                                          *
 *   Telemetry + debug-logging configuration.                              *
 *                                                                          *
 *   Both features ship DISABLED. They turn on at runtime ONLY when a       *
 *   marker file (named by METRICS_MARKER_FILE) sits in the same folder as  *
 *   the MiniVNC application. With the marker present, MiniVNC opens the     *
 *   statsd + log-sink UDP endpoints below and streams metrics and dprintf   *
 *   output to them; with it absent, nothing is opened and every emit is a   *
 *   no-op. See docs/TELEMETRY.md for the full walkthrough.                  *
 *                                                                          *
 *   The endpoints below are PLACEHOLDERS pointing at localhost. To aim      *
 *   them at your own collector without committing your infrastructure's     *
 *   addresses, copy MetricsConfig.local.h.example to MetricsConfig.local.h  *
 *   (which is git-ignored) and set the macros there — they win over the     *
 *   defaults below.                                                         *
 ****************************************************************************/

#ifndef MINIVNC_METRICSCONFIG_H
#define MINIVNC_METRICSCONFIG_H

/* Local, un-tracked override with real endpoints (optional). */
#if defined(__has_include)
    #if __has_include("MetricsConfig.local.h")
        #include "MetricsConfig.local.h"
    #endif
#endif

/* Name of the marker file that enables telemetry + debug logging. Drop a file
 * with this name (contents irrelevant) next to the MiniVNC app to turn them on.
 * Pascal string literal for the classic Mac File Manager. */
#ifndef METRICS_MARKER_FILE
    #define METRICS_MARKER_FILE "\pMiniVNC Telemetry"
#endif

/* statsd collector target (metrics). */
#ifndef STATSD_HOST
    #define STATSD_HOST "127.0.0.1"
#endif
#ifndef STATSD_PORT
    #define STATSD_PORT 8125
#endif

/* Plain-text log sink target (dprintf output). */
#ifndef LOG_HOST
    #define LOG_HOST "127.0.0.1"
#endif
#ifndef LOG_PORT
    #define LOG_PORT 5514
#endif

#endif /* MINIVNC_METRICSCONFIG_H */
