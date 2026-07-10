/*
 *  Telemetry + debug-logging endpoints. Both ship disabled and arm at
 *  runtime via a marker file next to the app (see docs/TELEMETRY.md).
 *  Defaults point at localhost; override them without committing your own
 *  addresses by copying MetricsConfig.local.h.example -> MetricsConfig.local.h.
 */

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
