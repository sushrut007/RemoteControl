# Coturn (TURN) for Darpan

Use when both peers are behind NAT and ICE cannot establish a direct path (common on mobile networks or double-NAT home links).

## Quick start

```bash
cd compose/docker-turn
cp .env.example .env
# Edit .env (external IP + password). Add to turnserver.conf:
#   user=${TURN_USERNAME}:${TURN_PASSWORD}
docker compose up -d
```

Open **UDP/TCP 3478** (and **5349** if using TLS TURN) on the host firewall.

## Client configuration

| Client | Where |
|--------|--------|
| **Darpan.exe** | Settings → STUN + TURN URL, username, password (or env `DARPAN_TURN_*`) |
| **Android** | Settings → STUN + TURN URL, username, password |

Example (UDP TURN):

- URL: `turn:203.0.113.10:3478?transport=udp`
- Username / password: from `.env`

STUN remains `stun:stun.l.google.com:19302` unless you run a private STUN on the same host.

## NAT test topology

See `docs/CROSS_NETWORK.md`.

Signaling stays on the Python server (`wss://` recommended). **No video or file bytes** pass through signaling or coturn control plane—only TURN relay carries media when ICE selects it.
