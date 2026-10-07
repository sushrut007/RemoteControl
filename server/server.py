#!/usr/bin/env python3
"""
Darpan signaling server (single file).

WebSocket JSON signaling only — no media, files, or input relay.
Run:  python server.py
Same env vars as before (DARPAN_HOST, DARPAN_PORT, DARPAN_TLS, …).
"""

from __future__ import annotations

import json
import logging
import os
import secrets
import string
import sys
import time
import uuid
from dataclasses import dataclass, field
from enum import Enum
from typing import Any

import bcrypt
import uvicorn
from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.responses import JSONResponse
from fastapi.middleware.cors import CORSMiddleware

# ---------------------------------------------------------------------------
# Config
# ---------------------------------------------------------------------------


def _env_bool(name: str, default: bool = False) -> bool:
    raw = os.getenv(name)
    if raw is None:
        return default
    return raw.strip().lower() in ("1", "true", "yes", "on")


@dataclass(frozen=True)
class Settings:
    host: str
    port: int
    tls_enabled: bool
    ssl_certfile: str | None
    ssl_keyfile: str | None
    max_room_size: int
    rate_limit_per_sec: int
    log_level: str
    max_message_bytes: int
    max_sdp_bytes: int

    @classmethod
    def from_env(cls) -> Settings:
        return cls(
            host=os.getenv("DARPAN_HOST", "0.0.0.0"),
            port=int(os.getenv("DARPAN_PORT", "8765")),
            tls_enabled=_env_bool("DARPAN_TLS", False),
            ssl_certfile=os.getenv("DARPAN_SSL_CERT") or None,
            ssl_keyfile=os.getenv("DARPAN_SSL_KEY") or None,
            max_room_size=int(os.getenv("DARPAN_MAX_ROOM_SIZE", "2")),
            rate_limit_per_sec=int(os.getenv("DARPAN_RATE_LIMIT_PER_SEC", "30")),
            log_level=os.getenv("DARPAN_LOG_LEVEL", "INFO").upper(),
            max_message_bytes=int(os.getenv("DARPAN_MAX_MESSAGE_BYTES", str(64 * 1024))),
            max_sdp_bytes=int(os.getenv("DARPAN_MAX_SDP_BYTES", str(256 * 1024))),
        )

    def validate_tls(self) -> None:
        if not self.tls_enabled:
            return
        if not self.ssl_certfile or not self.ssl_keyfile:
            raise ValueError("DARPAN_TLS requires DARPAN_SSL_CERT and DARPAN_SSL_KEY")


def load_settings() -> Settings:
    settings = Settings.from_env()
    settings.validate_tls()
    return settings


def configure_logging(level: str) -> None:
    root = logging.getLogger()
    root.handlers.clear()
    handler = logging.StreamHandler(sys.stdout)
    handler.setFormatter(
        logging.Formatter(
            fmt="%(asctime)s %(levelname)s %(name)s %(message)s",
            datefmt="%Y-%m-%dT%H:%M:%S",
        )
    )
    root.addHandler(handler)
    root.setLevel(getattr(logging, level, logging.INFO))


# ---------------------------------------------------------------------------
# Errors
# ---------------------------------------------------------------------------


class SignalingError(Exception):
    def __init__(self, code: str, message: str) -> None:
        super().__init__(message)
        self.code = code
        self.message = message


NOT_REGISTERED = "NOT_REGISTERED"
INVALID_MESSAGE = "INVALID_MESSAGE"
PAYLOAD_TOO_LARGE = "PAYLOAD_TOO_LARGE"
RATE_LIMITED = "RATE_LIMITED"
ROOM_NOT_FOUND = "ROOM_NOT_FOUND"
ROOM_FULL = "ROOM_FULL"
INVALID_PIN = "INVALID_PIN"
NOT_IN_ROOM = "NOT_IN_ROOM"
FORBIDDEN = "FORBIDDEN"
UNKNOWN_TARGET = "UNKNOWN_TARGET"
ALREADY_IN_ROOM = "ALREADY_IN_ROOM"

# ---------------------------------------------------------------------------
# Rate limit
# ---------------------------------------------------------------------------


class RateLimiter:
    def __init__(self, max_per_sec: int) -> None:
        self._max = max(1, max_per_sec)
        self._window_start = time.monotonic()
        self._count = 0

    def allow(self) -> bool:
        now = time.monotonic()
        if now - self._window_start >= 1.0:
            self._window_start = now
            self._count = 0
        if self._count >= self._max:
            return False
        self._count += 1
        return True


# ---------------------------------------------------------------------------
# Rooms
# ---------------------------------------------------------------------------

PROTOCOL_VERSION = 1
ROOM_ID_ALPHABET = string.ascii_uppercase + string.digits
ROOM_ID_LENGTH = 6


class Role(str, Enum):
    HOST = "host"
    CONTROLLER = "controller"
    VIEWER = "viewer"


class ControlState(str, Enum):
    VIEW_ONLY = "view_only"
    CONTROL_GRANTED = "control_granted"


@dataclass
class Member:
    device_id: str
    display_name: str
    role: Role
    control_state: ControlState = ControlState.VIEW_ONLY

    def to_public_dict(self) -> dict:
        out: dict = {
            "device_id": self.device_id,
            "display_name": self.display_name,
            "role": self.role.value,
        }
        if self.role != Role.HOST:
            out["control_state"] = self.control_state.value
        return out


@dataclass
class Room:
    room_id: str
    host_device_id: str | None = None
    pin_hash: bytes | None = None
    members: dict[str, Member] = field(default_factory=dict)

    def public_members(self) -> list[dict]:
        return [m.to_public_dict() for m in self.members.values()]


def hash_pin(pin: str) -> bytes:
    return bcrypt.hashpw(pin.encode("utf-8"), bcrypt.gensalt(rounds=12))


def verify_pin(pin: str, pin_hash: bytes) -> bool:
    try:
        return bcrypt.checkpw(pin.encode("utf-8"), pin_hash)
    except ValueError:
        return False


def generate_room_id(existing: set[str]) -> str:
    for _ in range(32):
        rid = "".join(secrets.choice(ROOM_ID_ALPHABET) for _ in range(ROOM_ID_LENGTH))
        if rid not in existing:
            return rid
    raise RuntimeError("failed to allocate unique room id")


class RoomStore:
    def __init__(self, max_room_size: int = 5) -> None:
        self.max_room_size = max_room_size
        self._rooms: dict[str, Room] = {}
        self._device_room: dict[str, str] = {}

    def device_room_id(self, device_id: str) -> str | None:
        return self._device_room.get(device_id)

    def get_room(self, room_id: str) -> Room | None:
        return self._rooms.get(room_id)

    def create_room(
        self,
        device_id: str,
        display_name: str,
        pin: str | None = None,
    ) -> Room:
        if device_id in self._device_room:
            raise SignalingError(ALREADY_IN_ROOM, "Leave current room before creating another")

        pin_hash = hash_pin(pin) if pin else None
        room_id = generate_room_id(set(self._rooms.keys()))
        host = Member(device_id=device_id, display_name=display_name, role=Role.HOST)
        room = Room(room_id=room_id, host_device_id=device_id, pin_hash=pin_hash, members={device_id: host})
        self._rooms[room_id] = room
        self._device_room[device_id] = room_id
        return room

    def join_room(
        self,
        device_id: str,
        display_name: str,
        room_id: str,
        pin: str | None = None,
        role_hint: str | None = None,
    ) -> tuple[Room, Member]:
        if device_id in self._device_room:
            raise SignalingError(ALREADY_IN_ROOM, "Leave current room before joining another")

        room = self._rooms.get(room_id)
        if room is None:
            raise SignalingError(ROOM_NOT_FOUND, "Room not found")

        if room.pin_hash is not None:
            if not pin or not verify_pin(pin, room.pin_hash):
                raise SignalingError(INVALID_PIN, "Invalid room PIN")

        if not room.members:
            member = Member(device_id=device_id, display_name=display_name, role=Role.HOST)
            room.host_device_id = device_id
            room.members[device_id] = member
            self._device_room[device_id] = room_id
            return room, member

        if len(room.members) >= self.max_room_size:
            raise SignalingError(
                ROOM_FULL,
                "Room is full — only one host and one controller are allowed",
            )

        sole = next(iter(room.members.values()))
        if room.host_device_id is None or sole.role != Role.HOST or room.host_device_id != sole.device_id:
            raise SignalingError(
                ROOM_FULL,
                "Room is full — only one host and one controller are allowed",
            )

        member = Member(device_id=device_id, display_name=display_name, role=Role.CONTROLLER)
        room.members[device_id] = member
        self._device_room[device_id] = room_id
        return room, member

    def leave_room(self, device_id: str) -> tuple[str | None, list[Member], Member | None]:
        room_id = self._device_room.pop(device_id, None)
        if room_id is None:
            raise SignalingError(NOT_IN_ROOM, "Not in a room")

        room = self._rooms.get(room_id)
        if room is None:
            return None, [], None

        departed = room.members.pop(device_id, None)
        remaining = list(room.members.values())

        if not room.members:
            room.host_device_id = None
            return room_id, [], departed

        if room.host_device_id == device_id:
            room.host_device_id = None

        return room_id, remaining, departed

    def promote_host(self, actor_device_id: str, target_device_id: str) -> Room:
        room_id = self._require_actor_in_room(actor_device_id)
        room = self._rooms[room_id]
        if room.host_device_id != actor_device_id:
            raise SignalingError(FORBIDDEN, "Only the host may promote another host")

        target = room.members.get(target_device_id)
        if target is None:
            raise SignalingError(UNKNOWN_TARGET, "Target not in room")

        old_host = room.members[room.host_device_id]
        old_host.role = Role.CONTROLLER
        old_host.control_state = ControlState.VIEW_ONLY

        target.role = Role.HOST
        target.control_state = ControlState.VIEW_ONLY
        room.host_device_id = target_device_id
        return room

    def grant_control(self, actor_device_id: str, target_device_id: str) -> Member:
        room = self._require_host(actor_device_id)
        target = room.members.get(target_device_id)
        if target is None:
            raise SignalingError(UNKNOWN_TARGET, "Target not in room")
        if target.role == Role.HOST:
            raise SignalingError(FORBIDDEN, "Cannot grant control to the host")
        if target.role == Role.VIEWER:
            raise SignalingError(FORBIDDEN, "Target must be a controller role")
        target.control_state = ControlState.CONTROL_GRANTED
        return target

    def revoke_control(self, actor_device_id: str, target_device_id: str) -> Member:
        room = self._require_host(actor_device_id)
        target = room.members.get(target_device_id)
        if target is None:
            raise SignalingError(UNKNOWN_TARGET, "Target not in room")
        target.control_state = ControlState.VIEW_ONLY
        return target

    def _require_actor_in_room(self, device_id: str) -> str:
        room_id = self._device_room.get(device_id)
        if room_id is None:
            raise SignalingError(NOT_IN_ROOM, "Not in a room")
        return room_id

    def _require_host(self, device_id: str) -> Room:
        room_id = self._require_actor_in_room(device_id)
        room = self._rooms[room_id]
        if room.host_device_id != device_id:
            raise SignalingError(FORBIDDEN, "Host only")
        return room


def relay_targets(
    room: Room,
    sender_device_id: str,
    target_device_id: str | None,
) -> list[str]:
    if target_device_id:
        if target_device_id not in room.members or target_device_id == sender_device_id:
            raise SignalingError(UNKNOWN_TARGET, "Invalid signal target")
        return [target_device_id]
    return [did for did in room.members if did != sender_device_id]


# ---------------------------------------------------------------------------
# WebSocket hub + FastAPI
# ---------------------------------------------------------------------------

log = logging.getLogger("darpan.signaling")

FORBIDDEN_CLIENT_TYPES = frozenset(
    {
        "video",
        "audio",
        "file",
        "file_chunk",
        "input",
        "input_event",
        "clipboard",
        "screen_frame",
    }
)

VALID_SIGNAL_KINDS = frozenset({"offer", "answer", "ice"})


@dataclass
class ConnectionState:
    websocket: WebSocket
    device_id: str | None = None
    display_name: str | None = None
    rate_limiter: RateLimiter = field(default_factory=lambda: RateLimiter(30))

    @property
    def registered(self) -> bool:
        return self.device_id is not None


class SignalingHub:
    def __init__(self, settings: Settings) -> None:
        self.settings = settings
        self.rooms = RoomStore(max_room_size=settings.max_room_size)
        self._connections: dict[str, ConnectionState] = {}

    async def register_connection(self, websocket: WebSocket) -> ConnectionState:
        await websocket.accept()
        state = ConnectionState(
            websocket=websocket,
            rate_limiter=RateLimiter(self.settings.rate_limit_per_sec),
        )
        return state

    def bind_device(self, state: ConnectionState, display_name: str) -> str:
        device_id = uuid.uuid4().hex
        state.device_id = device_id
        state.display_name = display_name.strip() or "Device"
        self._connections[device_id] = state
        log.info("device_registered device_id=%s display_name=%s", device_id, state.display_name)
        return device_id

    async def disconnect(self, state: ConnectionState) -> None:
        if not state.device_id:
            return
        device_id = state.device_id
        try:
            room_id, remaining, departed = self.rooms.leave_room(device_id)
            if room_id and departed and remaining:
                await self.broadcast_room(
                    room_id,
                    {
                        "v": PROTOCOL_VERSION,
                        "type": "member_left",
                        "room_id": room_id,
                        "device_id": device_id,
                    },
                )
                await self._emit_room_state(room_id)
                log.info("member_left room_id=%s device_id=%s", room_id, device_id)
        except SignalingError:
            pass
        self._connections.pop(device_id, None)
        log.info("device_disconnected device_id=%s", device_id)

    async def send_json(self, state: ConnectionState, payload: dict[str, Any]) -> None:
        await state.websocket.send_text(json.dumps(payload))

    async def send_error(self, state: ConnectionState, code: str, message: str) -> None:
        await self.send_json(
            state,
            {"v": PROTOCOL_VERSION, "type": "error", "code": code, "message": message},
        )

    async def broadcast_room(
        self,
        room_id: str,
        payload: dict[str, Any],
        *,
        exclude_device_id: str | None = None,
    ) -> None:
        room = self.rooms.get_room(room_id)
        if not room:
            return
        for device_id in room.members:
            if exclude_device_id and device_id == exclude_device_id:
                continue
            conn = self._connections.get(device_id)
            if conn:
                await self.send_json(conn, payload)

    async def handle_message(self, state: ConnectionState, raw: str) -> None:
        if len(raw.encode("utf-8")) > self.settings.max_message_bytes:
            await self.send_error(state, PAYLOAD_TOO_LARGE, "Message exceeds size limit")
            return

        if not state.rate_limiter.allow():
            await self.send_error(state, RATE_LIMITED, "Too many messages")
            return

        try:
            data = json.loads(raw)
        except json.JSONDecodeError:
            await self.send_error(state, INVALID_MESSAGE, "Invalid JSON")
            return

        if not isinstance(data, dict):
            await self.send_error(state, INVALID_MESSAGE, "Message must be a JSON object")
            return

        msg_type = data.get("type")
        if msg_type in FORBIDDEN_CLIENT_TYPES:
            await self.send_error(state, FORBIDDEN, "Message type not allowed on signaling server")
            return

        version = data.get("v")
        if version != PROTOCOL_VERSION:
            await self.send_error(state, INVALID_MESSAGE, "Unsupported protocol version")
            return

        log.debug("ws_in type=%s device_id=%s", msg_type, state.device_id)

        try:
            if msg_type == "hello":
                await self._handle_hello(state, data)
            elif msg_type == "ping":
                await self._handle_ping(state)
            else:
                if not state.registered:
                    await self.send_error(state, NOT_REGISTERED, "Send hello first")
                    return
                handlers = {
                    "create_room": self._handle_create_room,
                    "join_room": self._handle_join_room,
                    "leave_room": self._handle_leave_room,
                    "signal": self._handle_signal,
                    "grant_control": self._handle_grant_control,
                    "revoke_control": self._handle_revoke_control,
                    "request_control": self._handle_request_control,
                    "set_screen_share": self._handle_set_screen_share,
                    "promote_host": self._handle_promote_host,
                    "room_state": self._handle_room_state_request,
                }
                handler = handlers.get(msg_type)
                if handler is None:
                    await self.send_error(state, INVALID_MESSAGE, f"Unknown type: {msg_type}")
                    return
                await handler(state, data)
        except SignalingError as exc:
            await self.send_error(state, exc.code, exc.message)

    async def _handle_hello(self, state: ConnectionState, data: dict[str, Any]) -> None:
        if state.registered:
            await self.send_error(state, INVALID_MESSAGE, "Already registered")
            return
        name = data.get("device_name")
        if not isinstance(name, str):
            await self.send_error(state, INVALID_MESSAGE, "device_name required")
            return
        device_id = self.bind_device(state, name)
        await self.send_json(
            state,
            {"v": PROTOCOL_VERSION, "type": "hello_ok", "device_id": device_id},
        )

    async def _handle_ping(self, state: ConnectionState) -> None:
        await self.send_json(state, {"v": PROTOCOL_VERSION, "type": "pong"})

    async def _handle_create_room(self, state: ConnectionState, data: dict[str, Any]) -> None:
        pin = data.get("pin")
        if pin is not None and not isinstance(pin, str):
            raise SignalingError(INVALID_MESSAGE, "pin must be a string")
        room = self.rooms.create_room(state.device_id, state.display_name or "Device", pin)
        log.info("room_created room_id=%s host=%s", room.room_id, state.device_id)
        await self.send_json(
            state,
            {
                "v": PROTOCOL_VERSION,
                "type": "room_created",
                "room_id": room.room_id,
                "role": "host",
                "members": room.public_members(),
            },
        )
        await self._emit_room_state(room.room_id)

    async def _handle_join_room(self, state: ConnectionState, data: dict[str, Any]) -> None:
        room_id = data.get("room_id")
        if not isinstance(room_id, str):
            raise SignalingError(INVALID_MESSAGE, "room_id required")
        pin = data.get("pin")
        if pin is not None and not isinstance(pin, str):
            raise SignalingError(INVALID_MESSAGE, "pin must be a string")
        role_hint = data.get("role_hint")
        if role_hint is not None and not isinstance(role_hint, str):
            raise SignalingError(INVALID_MESSAGE, "role_hint must be a string")

        room, member = self.rooms.join_room(
            state.device_id,
            state.display_name or "Device",
            room_id.strip().upper(),
            pin=pin,
            role_hint=role_hint,
        )
        log.info("room_joined room_id=%s device_id=%s role=%s", room.room_id, state.device_id, member.role.value)
        await self.send_json(
            state,
            {
                "v": PROTOCOL_VERSION,
                "type": "room_joined",
                "room_id": room.room_id,
                "role": member.role.value,
                "members": room.public_members(),
            },
        )
        await self.broadcast_room(
            room.room_id,
            {
                "v": PROTOCOL_VERSION,
                "type": "member_joined",
                "room_id": room.room_id,
                "member": member.to_public_dict(),
            },
            exclude_device_id=state.device_id,
        )
        await self._emit_room_state(room.room_id)

    async def _handle_leave_room(self, state: ConnectionState, data: dict[str, Any]) -> None:
        room_id, remaining, departed = self.rooms.leave_room(state.device_id)
        if departed is None:
            raise SignalingError(NOT_IN_ROOM, "Not in a room")
        await self.send_json(
            state,
            {"v": PROTOCOL_VERSION, "type": "room_left", "room_id": room_id},
        )
        if room_id and remaining:
            await self.broadcast_room(
                room_id,
                {
                    "v": PROTOCOL_VERSION,
                    "type": "member_left",
                    "room_id": room_id,
                    "device_id": state.device_id,
                },
            )
            await self._emit_room_state(room_id)

    async def _handle_signal(self, state: ConnectionState, data: dict[str, Any]) -> None:
        room_id = self.rooms.device_room_id(state.device_id)
        if room_id is None:
            raise SignalingError(NOT_IN_ROOM, "Not in a room")

        signal_type = data.get("signal_type")
        if signal_type not in VALID_SIGNAL_KINDS:
            raise SignalingError(INVALID_MESSAGE, "signal_type must be offer, answer, or ice")

        sdp = data.get("sdp")
        candidate = data.get("candidate")
        if signal_type in ("offer", "answer"):
            if not isinstance(sdp, str):
                raise SignalingError(INVALID_MESSAGE, "sdp required for offer/answer")
            if len(sdp.encode("utf-8")) > self.settings.max_sdp_bytes:
                raise SignalingError(PAYLOAD_TOO_LARGE, "SDP exceeds size limit")
        if signal_type == "ice":
            if candidate is None:
                raise SignalingError(INVALID_MESSAGE, "candidate required for ice")

        target_device_id = data.get("target_device_id")
        if target_device_id is not None and not isinstance(target_device_id, str):
            raise SignalingError(INVALID_MESSAGE, "target_device_id must be a string")

        room = self.rooms.get_room(room_id)
        if room is None:
            raise SignalingError(NOT_IN_ROOM, "Room not found")

        targets = relay_targets(room, state.device_id, target_device_id)
        payload = {
            "v": PROTOCOL_VERSION,
            "type": "signal",
            "room_id": room_id,
            "from_device_id": state.device_id,
            "signal_type": signal_type,
        }
        if sdp is not None:
            payload["sdp"] = sdp
        if candidate is not None:
            payload["candidate"] = candidate

        log.info(
            "signal_relay room_id=%s from=%s signal_type=%s targets=%s",
            room_id,
            state.device_id,
            signal_type,
            len(targets),
        )

        for target in targets:
            conn = self._connections.get(target)
            if conn:
                await self.send_json(conn, payload)

    async def _handle_grant_control(self, state: ConnectionState, data: dict[str, Any]) -> None:
        target = data.get("target_device_id")
        if not isinstance(target, str):
            raise SignalingError(INVALID_MESSAGE, "target_device_id required")
        member = self.rooms.grant_control(state.device_id, target)
        room_id = self.rooms.device_room_id(state.device_id)
        await self.broadcast_room(
            room_id,
            {
                "v": PROTOCOL_VERSION,
                "type": "control_state",
                "room_id": room_id,
                "device_id": target,
                "state": member.control_state.value,
            },
        )

    async def _handle_revoke_control(self, state: ConnectionState, data: dict[str, Any]) -> None:
        target = data.get("target_device_id")
        if not isinstance(target, str):
            raise SignalingError(INVALID_MESSAGE, "target_device_id required")
        member = self.rooms.revoke_control(state.device_id, target)
        room_id = self.rooms.device_room_id(state.device_id)
        await self.broadcast_room(
            room_id,
            {
                "v": PROTOCOL_VERSION,
                "type": "control_state",
                "room_id": room_id,
                "device_id": target,
                "state": member.control_state.value,
            },
        )

    async def _handle_request_control(self, state: ConnectionState, data: dict[str, Any]) -> None:
        room_id = self.rooms.device_room_id(state.device_id)
        if room_id is None:
            raise SignalingError(NOT_IN_ROOM, "Not in a room")
        room = self.rooms.get_room(room_id)
        if room is None:
            raise SignalingError(ROOM_NOT_FOUND, "Room not found")
        if state.device_id == room.host_device_id:
            raise SignalingError(FORBIDDEN, "Host cannot request control from itself")
        host_conn = self._connections.get(room.host_device_id)
        if host_conn is None:
            raise SignalingError(FORBIDDEN, "Host is offline")
        member = room.members.get(state.device_id)
        display_name = member.display_name if member else "Peer"
        await self.send_json(
            host_conn,
            {
                "v": PROTOCOL_VERSION,
                "type": "control_request",
                "room_id": room_id,
                "device_id": state.device_id,
                "display_name": display_name,
            },
        )

    async def _handle_set_screen_share(self, state: ConnectionState, data: dict[str, Any]) -> None:
        active = data.get("active")
        if not isinstance(active, bool):
            raise SignalingError(INVALID_MESSAGE, "active must be a boolean")
        self.rooms._require_host(state.device_id)
        room_id = self.rooms.device_room_id(state.device_id)
        if room_id is None:
            raise SignalingError(NOT_IN_ROOM, "Not in a room")
        await self.broadcast_room(
            room_id,
            {
                "v": PROTOCOL_VERSION,
                "type": "screen_share_state",
                "room_id": room_id,
                "device_id": state.device_id,
                "active": active,
            },
        )

    async def _handle_promote_host(self, state: ConnectionState, data: dict[str, Any]) -> None:
        target = data.get("target_device_id")
        if not isinstance(target, str):
            raise SignalingError(INVALID_MESSAGE, "target_device_id required")
        room = self.rooms.promote_host(state.device_id, target)
        await self._emit_room_state(room.room_id)

    async def _handle_room_state_request(self, state: ConnectionState, data: dict[str, Any]) -> None:
        room_id = self.rooms.device_room_id(state.device_id)
        if room_id is None:
            raise SignalingError(NOT_IN_ROOM, "Not in a room")
        await self._emit_room_state(room_id, target=state)

    async def _emit_room_state(
        self,
        room_id: str,
        *,
        target: ConnectionState | None = None,
    ) -> None:
        room = self.rooms.get_room(room_id)
        if room is None:
            return
        payload = {
            "v": PROTOCOL_VERSION,
            "type": "room_state",
            "room_id": room_id,
            "host_device_id": room.host_device_id,
            "members": room.public_members(),
        }
        if target:
            await self.send_json(target, payload)
        else:
            await self.broadcast_room(room_id, payload)


def create_app(settings: Settings | None = None) -> FastAPI:
    settings = settings or load_settings()
    hub = SignalingHub(settings)

    app = FastAPI(title="Darpan Signaling", version="0.1.0")

    app.add_middleware(
        CORSMiddleware,
        allow_origins=[
            "https://remotecontrol.sushrutmakes.qzz.io",
            "http://localhost:3000",
            "http://127.0.0.1:3000",
        ],
        allow_credentials=True,
        allow_methods=["*"],
        allow_headers=["*"],
    )

    @app.get("/health")
    async def health() -> JSONResponse:
        return JSONResponse({"status": "ok", "service": "darpan-signaling"})

    @app.websocket("/ws")
    async def websocket_endpoint(websocket: WebSocket) -> None:
        state = await hub.register_connection(websocket)
        try:
            while True:
                raw = await websocket.receive_text()
                await hub.handle_message(state, raw)
        except WebSocketDisconnect:
            await hub.disconnect(state)
        except Exception:
            log.exception("websocket_error device_id=%s", state.device_id)
            await hub.disconnect(state)

    app.state.hub = hub
    app.state.settings = settings
    return app


# Create a global app instance for Vercel ASGI
app = create_app()


def main() -> None:
    settings = load_settings()
    configure_logging(settings.log_level)

    log.info(
        "Signaling listening on %s:%s (health http://<your-lan-ip>:%s/health, ws ws://<your-lan-ip>:%s/ws)",
        settings.host,
        settings.port,
        settings.port,
        settings.port,
    )
    if settings.host == "0.0.0.0":
        log.info("Bound to all interfaces — other PCs use your LAN IP, not 127.0.0.1")

    ssl_kwargs: dict[str, str] = {}
    if settings.tls_enabled:
        ssl_kwargs = {
            "ssl_certfile": settings.ssl_certfile or "",
            "ssl_keyfile": settings.ssl_keyfile or "",
        }

    uvicorn.run(
        app,
        host=settings.host,
        port=settings.port,
        log_level=settings.log_level.lower(),
        **ssl_kwargs,
    )


if __name__ == "__main__":
    main()
