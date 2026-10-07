# Darpan signaling server

Single-file Python service: **`server.py`**. WebSocket JSON for rooms and WebRTC signaling relay only (no video, files, or input).

## Quick start

```powershell
cd C:\Gitlab-repo\darpan\server
python -m venv .venv
.\.venv\Scripts\activate
pip install -r requirements.txt
pip install -e ".[dev]"   # optional: pytest
python server.py
```

LAN / other PCs:

```powershell
$env:DARPAN_HOST = "0.0.0.0"
python server.py
```

Default: **`0.0.0.0:8765`**. Health check: `GET /health`. WebSocket: `/ws`.

Clients: Settings → `ws://YOUR_IP:8765/ws`

## Layout

| Path | Purpose |
|------|---------|
| `server.py` | All signaling logic (run this) |
| `requirements.txt` | Runtime dependencies |
| `pyproject.toml` | Optional editable install + pytest |
| `tests/` | Unit / integration tests |
| `Dockerfile` | Container that runs `python server.py` |

## Environment variables

| Variable | Default | Description |
|----------|---------|-------------|
| `DARPAN_HOST` | `0.0.0.0` | Bind address |
| `DARPAN_PORT` | `8765` | Listen port |
| `DARPAN_TLS` | `0` | Set `1` for TLS |
| `DARPAN_SSL_CERT` / `DARPAN_SSL_KEY` | — | PEM paths when TLS enabled |
| `DARPAN_MAX_ROOM_SIZE` | `2` | Max peers per room (one host + one controller) |
| `DARPAN_RATE_LIMIT_PER_SEC` | `30` | Inbound WS messages per second per connection |
| `DARPAN_LOG_LEVEL` | `INFO` | Log level |
| `DARPAN_MAX_MESSAGE_BYTES` | `65536` | Max JSON frame size |
| `DARPAN_MAX_SDP_BYTES` | `262144` | Max SDP in `signal` messages |

## Tests

```powershell
pytest
```

## Docker

```powershell
docker build -t darpan-signaling .
docker run --rm -p 8765:8765 darpan-signaling
```

Protocol details: `../docs/ARCHITECTURE.md` and `../docs/PROTOCOL.md`.
