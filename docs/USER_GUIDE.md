# Darpan — User guide

Darpan connects two devices in a **room** (short code) for remote viewing, optional remote control (with host approval), and file transfer on desktop. Signaling is online; **screen and file data stay peer-to-peer** over WebRTC.

---

## Concepts

| Term | Meaning |
|------|---------|
| **Host** | Creates the room; shares their screen (PC monitor or Android via screen capture) |
| **Joiner / controller** | Joins with the room code; views the host screen |
| **View only** | Default — can see the screen but not inject input |
| **Control granted** | Host approved remote mouse/keyboard from one joiner |

---

## Desktop — Host a session

1. Start the signaling server (or use your team’s `wss://` URL).
2. Open **Darpan.exe** → **Settings** → set **Signaling WebSocket URL** (use `wss://…/ws` when TLS is enabled), STUN, optional TURN.
3. **Host session** → optional **room PIN** → **Create room**.
4. Share the **room code** (and PIN if set) with the joiner.
5. When a peer connects, your **privacy banner** shows that sharing is active. Preview starts automatically.
6. If someone **requests control**, a dialog asks you to **Allow** or deny.
7. **Revoke control** anytime while you remain host.
8. **Leave room** or close the app to end the session (disconnects cleanly).

---

## Desktop — Join a session

1. **Join session** → enter **room code** and PIN if required → **Join room**.
2. Wait for **Peer connected** — the remote screen appears on the right.
3. **Request control** — the host must approve on their PC.
4. Move mouse and type in the viewer when status shows control granted.
5. **Send file…** (when connected) — pick a file; accept save location on the receiving side.
6. **Reconnect signaling** / **Reconnect video** if the network drops (see status line).

---

## Allow remote control (host)

- Only the **PC host** grants control via the prompt or implicit grant flow.
- Joiners send **`request_control`**; you see **Allow remote control?**
- **Revoke control** removes injection immediately; joiner returns to view-only.
- Input is ignored unless server state is **`control_granted`** (see `docs/PROTOCOL.md`).

---

## File transfer (desktop)

- Uses WebRTC data channel **`darpan.files`** (not the signaling server).
- Host or joiner can **Send file…** when the peer session is up.
- Receiver chooses save path on incoming offers.

---

## Android pairing

Android app: **`com.darpan.remote`** (Material 3, same room codes as desktop).

### Control a Windows PC

1. **Settings** → device name, **`wss://`** signaling URL, STUN/TURN (match desktop when crossing NAT).
2. **Control a PC** → room code (+ PIN) → **Join room**.
3. **Request control** → approve on the **PC host**.
4. Touch moves the mouse; **Keyboard** sends common keys.

### Share Android screen to PC

1. **Share screen to PC** → accept Android screen-capture consent → room is created.
2. On **Darpan.exe**, **Join session** with the same code.
3. PC views the phone screen. **Remote control of the phone from PC is view-only in v1** (`docs/ANDROID_SCOPE.md`).

### iOS

Not supported — Android and Windows only.

---

## Privacy

- **Desktop host:** red banner while peers can see your display.
- **Android:** banner while in an active room.
- Closing the app or **Leave room** ends signaling membership and stops capture.

---

## Cross-network tips

If both sides are on different networks and video never connects, configure **TURN** on both clients using the same relay (`docs/CROSS_NETWORK.md`).

---

## Getting help

- Build/deploy: `docs/BUILD.md`
- Protocol details: `docs/PROTOCOL.md`, `docs/ARCHITECTURE.md`
- Pre-demo checklist: `docs/RELEASE_CHECKLIST.md`
