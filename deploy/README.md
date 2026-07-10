# MiniVNC telemetry infrastructure

Collector-side infra for MiniVNC's opt-in metrics + debug log (see
[`../docs/TELEMETRY.md`](../docs/TELEMETRY.md)). Two interchangeable deployments;
pick whichever matches where you run your collector.

Both expose the same two ingress ports the Mac sends to:

| Port | Proto | Purpose |
|------|-------|---------|
| 8125 | UDP | statsd metrics → OpenTelemetry Collector |
| 5514 | UDP | `dprintf` lines → log sink (stdout) |

Whichever you run, set the matching `STATSD_HOST` / `LOG_HOST` in
`mac-cpp-source/MetricsConfig.local.h`, and drop the `MiniVNC Telemetry` marker
file next to the app to arm it.

---

## Docker Compose (local / single host)

```sh
docker compose -f deploy/docker-compose/compose.yaml up
```

Brings up:

- **otel-collector** — statsd receiver → Prometheus exporter (`:8889`)
- **prometheus** — scrapes the collector, UI at <http://localhost:9090>
- **log-sink** — UDP → stdout; read with
  `docker compose -f deploy/docker-compose/compose.yaml logs -f log-sink`

Point the Mac at this host's LAN IP (ports 8125 / 5514).

## Kubernetes (the debene cluster)

```sh
kubectl --context debene apply -f deploy/otel-collector.yaml   # statsd → Prometheus (+ ServiceMonitor)
kubectl --context debene apply -f deploy/log-sink.yaml         # dprintf → stdout
kubectl --context debene apply -f deploy/grafana-dashboard.yaml
```

Ingress is via MetalLB VIPs (`10.0.100.116` statsd, `10.0.100.114` log) so the
Mac sends straight to them. Read the debug log with:

```sh
kubectl --context debene -n gopher-spot logs -f deploy/log-sink
```

> The log path is UDP and best-effort — bursts can drop lines. For byte-exact
> debugging, `tcpdump` the VNC TCP stream on the client instead.
