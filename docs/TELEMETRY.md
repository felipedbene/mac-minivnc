# MiniVNC Telemetry & Debug Logging

MiniVNC can emit two kinds of diagnostics over UDP:

- **Metrics** — statsd-format counters/gauges (frame timings, connection
  lifecycle, boot stages) for a collector such as the OpenTelemetry Collector.
- **Debug log** — the server's `dprintf` output streamed line-by-line to a
  plain-text UDP "log sink".

Both are **opt-in and ship disabled.** A stock build sends nothing on the wire
beyond the VNC protocol itself, and every `metrics_*` / `dprintf` call compiles
to a cheap no-op until you explicitly arm it.

---

## Turning it on: the marker file

Telemetry and debug logging are armed at **runtime** by the presence of a
**marker file next to the MiniVNC application**:

```
(volume)
└── MiniVNC              ← the application
    MiniVNC Telemetry    ← create this file (any type, contents ignored) to arm
```

On launch MiniVNC looks for a file named **`MiniVNC Telemetry`** in its own
folder (via `TelemetryMarkerPresent()` in `main.cpp`). If it's there:

- `statsd_open()` / `statsd_log_open()` bring up the UDP endpoints, and
- `vncConfig.enableLogging` is set, so `dprintf` starts streaming.

If it's absent, none of that happens. To disable again, delete the file and
relaunch. (The name can be changed via `METRICS_MARKER_FILE`, below.)

A file (rather than a UI toggle) arms diagnostics on a headless or Startup-Items
deployment without a rebuild, and keeps the shipping default off.

---

## Pointing it at your collector: `MetricsConfig.h`

The endpoints live in [`mac-cpp-source/MetricsConfig.h`](../mac-cpp-source/MetricsConfig.h),
which ships with **localhost placeholders**:

| Macro | Default | Meaning |
|-------|---------|---------|
| `STATSD_HOST` / `STATSD_PORT` | `127.0.0.1:8125` | statsd metrics destination |
| `LOG_HOST` / `LOG_PORT`       | `127.0.0.1:5514` | plain-text log-sink destination |
| `METRICS_MARKER_FILE`         | `"\pMiniVNC Telemetry"` | marker file name (Pascal string) |

To aim these at your own infrastructure **without committing your addresses**,
copy the template to a git-ignored local override:

```sh
cd mac-cpp-source
cp MetricsConfig.local.h.example MetricsConfig.local.h
$EDITOR MetricsConfig.local.h        # set your STATSD_HOST / LOG_HOST / ports
```

`MetricsConfig.local.h` is listed in `.gitignore`; its `#define`s win over the
defaults in `MetricsConfig.h`. If it's absent, the localhost defaults apply.

---

## The collector side (reproducible infra)

Ready-to-run collector manifests live under [`deploy/`](../deploy/):

- **Kubernetes** — `deploy/otel-collector.yaml` (statsd receiver → Prometheus
  exporter), `deploy/log-sink.yaml` (UDP → stdout), `deploy/grafana-dashboard.yaml`.
- **Docker Compose** — `deploy/docker-compose/` brings the same collector + log
  sink + Prometheus up locally with `docker compose up`.

See [`deploy/README.md`](../deploy/README.md) for both paths.

### Reading the debug log

- Kubernetes: `kubectl -n gopher-spot logs -f deploy/log-sink`
- Docker Compose: `docker compose -f deploy/docker-compose/compose.yaml logs -f log-sink`

> Note: the log sink is UDP and best-effort — under a burst (e.g. a per-frame
> hex dump) lines can drop. For byte-exact debugging, capture the TCP stream on
> the client with `tcpdump`/Wireshark instead.
