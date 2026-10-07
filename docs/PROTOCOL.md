# Darpan P2P binary protocol (WebRTC DataChannels)

All multi-byte integers are **little-endian**. First byte is always **`version`** (`0x01`).

## Channels

| Label | Direction | Purpose |
|-------|-----------|---------|
| `darpan.preview` | Host → viewer | Screen frames (JPEG) + stream metadata prefix |
| `darpan.control` | Controller → host | Input events (host ignores unless control granted) |
| `darpan.files` | Bidirectional | File transfer |

Signaling server never carries these payloads.

---

## Preview frames (`darpan.preview`)

```
Offset  Size  Field
0       4     Magic "DPJ1"
4       2     Stream width (pixels)
6       2     Stream height (pixels)
8       …     JPEG image bytes
```

Viewer maps pointer coordinates into `[0, width] × [0, height]` before sending control messages.

---

## Control messages (`darpan.control`)

| Type | Name | Payload after header |
|------|------|---------------------|
| `0x10` | MouseMove | `u16 x`, `u16 y` normalized 0–65535 |
| `0x11` | MouseButton | `u8 button` (0=L,1=R,2=M), `u8 down` (1 down, 0 up), `u16 x`, `u16 y` |
| `0x12` | Wheel | `i16 delta`, `u16 x`, `u16 y` |
| `0x20` | KeyDown | `u16 vk`, `u16 flags` (bit0 extended) |
| `0x21` | KeyUp | `u16 vk`, `u16 flags` |

**Header (all control types):** `u8 version`, `u8 type`

Host maps normalized coordinates to **primary monitor pixel space** using the stored stream width/height from the latest preview frame.

---

## File messages (`darpan.files`)

**Header:** `u8 version`, `u8 type`

| Type | Name | Body |
|------|------|------|
| `0x30` | Offer | `u32 transfer_id`, `u64 size`, `u8 name_len`, `name utf8`, `32 bytes sha256` (0 = unknown) |
| `0x31` | Accept | `u32 transfer_id` |
| `0x32` | Reject | `u32 transfer_id` |
| `0x33` | Chunk | `u32 transfer_id`, `u64 offset`, `u32 len`, `data` |
| `0x34` | Complete | `u32 transfer_id`, `32 bytes sha256` |
| `0x35` | Cancel | `u32 transfer_id` |

Chunk size default: **32 KiB**. Receiver verifies SHA-256 on `Complete` before marking success.

Resume (optional): if receiver already has partial file, it may send `Accept` after creating file and seek to existing length; sender continues from offset (same `transfer_id`).

---

## Signaling (control permission)

Permission is **not** inferred from the data channel. The host uses signaling:

- `request_control` → host UI prompt
- `grant_control` / `revoke_control` → `control_state` broadcast

Input on `darpan.control` is applied on the host only while the target controller has `control_granted` in room state.

---

## Signaling server — forbidden payloads

The Python server **must not** accept client message types that carry media, files, or input. These return **`FORBIDDEN`** (implemented in `server/server.py`):

`video`, `audio`, `file`, `file_chunk`, `input`, `input_event`, `clipboard`, `screen_frame`

Allowed signaling includes: `hello`, `create_room`, `join_room`, `leave_room`, `signal` (SDP/ICE only), `request_control`, `grant_control`, `revoke_control`, `ping`, `room_state`, etc.

Regression tests: `server/tests/test_forbidden_signaling_payloads.py`.
