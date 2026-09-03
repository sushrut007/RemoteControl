"""
Signaling Server for Remote Device Control (Pure P2P / CrossDesk Architecture)
FastAPI + python-socketio

Relays room registration, SDP offer/answer, and ICE candidates between peers.
Zero media or data channel packets pass through this server once P2P is established!
"""

from __future__ import annotations

import logging
import os
import uuid
from typing import Any, Dict, List, Optional

import socketio
import uvicorn
from dotenv import load_dotenv
from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware

logging.basicConfig(
    level=logging.DEBUG,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
)
log = logging.getLogger("signaling")

load_dotenv()

PORT: int = int(os.getenv("PORT", "5000"))
HOST: str = os.getenv("HOST", "0.0.0.0")
CORS_ORIGINS = [
    "https://remotecontrol.sushrutmakes.qzz.io",
    "http://localhost",
    "http://localhost:3000",
    "http://localhost:5173",
    "*"
]

# ---------------------------------------------------------------------------
# In-Memory State
# ---------------------------------------------------------------------------
# rooms: roomId -> dict of peerId -> { sid, displayName, appType, metadata }
rooms: Dict[str, Dict[str, Dict[str, Any]]] = {}
# sid_to_peer: sid -> (roomId, peerId)
sid_to_peer: Dict[str, tuple[str, str]] = {}

sio = socketio.AsyncServer(
    async_mode="asgi",
    cors_allowed_origins="*",
    ping_timeout=20,
    ping_interval=10,
    logger=False,
    engineio_logger=False,
)

fastapi_app = FastAPI(title="RemoteControl P2P Signaling Server")
fastapi_app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)
app = socketio.ASGIApp(sio, other_asgi_app=fastapi_app)

@fastapi_app.get("/health")
async def health():
    return {"status": "ok", "active_rooms": len(rooms)}

# ---------------------------------------------------------------------------
# Socket.IO Event Handlers
# ---------------------------------------------------------------------------

@sio.event
async def connect(sid, environ, auth=None):
    log.info(f"[SIO] Connected: {sid}")

@sio.event
async def disconnect(sid):
    log.info(f"[SIO] Disconnected: {sid}")
    if sid not in sid_to_peer:
        return

    room_id, peer_id = sid_to_peer.pop(sid)
    if room_id in rooms and peer_id in rooms[room_id]:
        rooms[room_id].pop(peer_id, None)
        log.info(f"[SIO] Peer {peer_id} removed from room {room_id}")

        # Notify remaining peers
        await sio.emit("peer-left", {"peerId": peer_id}, room=room_id)
        if not rooms[room_id]:
            rooms.pop(room_id, None)
            log.info(f"[SIO] Room {room_id} deleted (empty)")

@sio.on("join-room")
async def handle_join_room(sid, data):
    room_id = data.get("roomId", "default")
    display_name = data.get("displayName", "User")
    app_type = data.get("appType", "viewer")
    metadata = data.get("metadata", {})

    peer_id = f"peer_{uuid.uuid4().hex[:8]}"

    if room_id not in rooms:
        rooms[room_id] = {}

    existing_peers = []
    for pid, pinfo in rooms[room_id].items():
        existing_peers.append({
            "peerId": pid,
            "id": pid,
            "displayName": pinfo["displayName"],
            "appType": pinfo["appType"],
            "metadata": pinfo["metadata"]
        })

    rooms[room_id][peer_id] = {
        "sid": sid,
        "peerId": peer_id,
        "displayName": display_name,
        "appType": app_type,
        "metadata": metadata
    }
    sid_to_peer[sid] = (room_id, peer_id)

    # Join Socket.IO room channel
    await sio.enter_room(sid, room_id)
    log.info(f"[SIO] Peer {peer_id} ({app_type}) joined room {room_id}")

    # Notify others in the room
    await sio.emit(
        "peer-joined",
        [{
            "peerId": peer_id,
            "id": peer_id,
            "displayName": display_name,
            "appType": app_type,
            "metadata": metadata
        }],
        room=room_id,
        skip_sid=sid
    )

    # Return ack to joining peer
    return {
        "peerId": peer_id,
        "peers": existing_peers
    }

@sio.on("viewer-ready")
async def handle_viewer_ready(sid, data):
    if sid not in sid_to_peer:
        return
    room_id, peer_id = sid_to_peer[sid]
    # Forward to host in room
    await sio.emit("viewer-ready", {"peerId": peer_id}, room=room_id, skip_sid=sid)

@sio.on("p2p-signal")
async def handle_p2p_signal(sid, data):
    if sid not in sid_to_peer:
        return
    room_id, peer_id = sid_to_peer[sid]
    # Relay offer or answer to other peer in the room
    await sio.emit("p2p-signal", [data], room=room_id, skip_sid=sid)

@sio.on("p2p-candidate")
async def handle_p2p_candidate(sid, data):
    if sid not in sid_to_peer:
        return
    room_id, peer_id = sid_to_peer[sid]
    # Relay ICE candidate to other peer in the room
    await sio.emit("p2p-candidate", [data], room=room_id, skip_sid=sid)

@sio.on("stream-ready")
async def handle_stream_ready(sid, data):
    if sid not in sid_to_peer:
        return
    room_id, _ = sid_to_peer[sid]
    await sio.emit("stream-ready", [data], room=room_id, skip_sid=sid)

@sio.on("stream-stopped")
async def handle_stream_stopped(sid, data):
    if sid not in sid_to_peer:
        return
    room_id, _ = sid_to_peer[sid]
    await sio.emit("stream-stopped", [data], room=room_id, skip_sid=sid)

@sio.on("leave-room")
async def handle_leave_room(sid, data):
    await disconnect(sid)

@sio.on("kick-peer")
async def handle_kick_peer(sid, data):
    target_peer_id = data.get("peerId")
    if sid not in sid_to_peer:
        return
    room_id, _ = sid_to_peer[sid]

    if room_id in rooms and target_peer_id in rooms[room_id]:
        target_sid = rooms[room_id][target_peer_id]["sid"]
        await sio.emit("kicked", {}, to=target_sid)
        await sio.disconnect(target_sid)

if __name__ == "__main__":
    uvicorn.run(app, host=HOST, port=PORT, log_level="info")
