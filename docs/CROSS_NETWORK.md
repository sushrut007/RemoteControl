# Cross-network and NAT (STUN + TURN)

## Roles

| Component | Purpose |
|-----------|---------|
| **Signaling** (`server.py`) | WebSocket JSON only — rooms, SDP/ICE relay, control permission |
| **STUN** | Helps peers discover public/reflexive addresses |
| **TURN** (`compose/docker-turn/`) | Relays media when direct UDP/TCP candidate pairs fail |

Preview (DPJ1), control, and file bytes use **WebRTC data channels** (desktop) or the same channels on Android. They never traverse the signaling HTTP/WebSocket API.

## Recommended test topology

```text
                    ┌─────────────────┐
                    │ Signaling (TLS) │
                    │  wss://host/ws  │
                    └────────┬────────┘
                             │
     ┌───────────────────────┼───────────────────────┐
     │                       │                       │
┌────▼────┐             ┌─────▼─────┐           ┌─────▼─────┐
│ PC A    │             │ STUN      │           │ PC B or   │
│ (Host)  │◄─── ICE ───►│ (public)  │◄── ICE ──►│ Android   │
│ NAT #1  │             └───────────┘           │ NAT #2    │
└────┬────┘                                     └─────┬─────┘
     │                                                 │
     └──────────────► TURN (coturn) ◄──────────────────┘
                  (only if direct path fails)
```

### Scenario 1 — Same LAN (STUN only)

- Both clients on one Wi‑Fi; signaling on a LAN IP or localhost with TLS terminator.
- Expected: ICE **host/srflx** candidates succeed; TURN unused.

### Scenario 2 — Two home networks (TURN likely)

- Host on network A, joiner on network B (or Android on cellular).
- Run coturn on a **VPS or cloud VM** with a public IP (`compose/docker-turn/`).
- Configure **identical** TURN URL + username + password on **both** clients.
- Expected: connection succeeds; coturn logs show `ALLOCATE` / relay usage when symmetric NAT blocks direct UDP.

### Scenario 3 — Android + desktop

- Desktop hosts, phone joins as controller (or reverse for screen share).
- Phone must reach **wss** signaling and **UDP/TCP to TURN** (carrier may block some ports — try `?transport=tcp` on TURN URL).

## Verification checklist

1. Signaling: both sides show registered / in room (no media in server logs).
2. ICE state: desktop status **Peer connected**; Android **Peer connected**.
3. Without TURN on symmetric NAT: expect failure or prolonged **negotiating**.
4. With TURN: retry with coturn credentials — preview should appear within ~30s.
5. Server pytest: `test_forbidden_signaling_payloads.py` confirms media types are rejected.

## Environment variables (desktop optional override)

| Variable | Maps to |
|----------|---------|
| `DARPAN_TURN_URL` | TURN server URI |
| `DARPAN_TURN_USERNAME` | TURN username |
| `DARPAN_TURN_PASSWORD` | TURN password |

Settings UI values apply when env vars are unset. Env wins when set (see `Utils/AppSettings.cpp`).
