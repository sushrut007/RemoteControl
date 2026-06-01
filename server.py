"""
Signaling Server for Remote Device Control
FastAPI + python-socketio + mediasoup (Node.js subprocess worker via JSON-RPC)

Requirements:
    pip install fastapi python-socketio uvicorn python-dotenv

    Node.js + mediasoup must be installed for the SFU worker:
        npm install mediasoup   (in the same directory or any directory on PATH)
"""

from __future__ import annotations

import asyncio
import json
import logging
import os
import tempfile
import uuid
from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional

import socketio
import uvicorn
from dotenv import load_dotenv
from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware

# ---------------------------------------------------------------------------
# Logging
# ---------------------------------------------------------------------------
logging.basicConfig(
    level=logging.DEBUG,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
)
log = logging.getLogger("signaling")

# ---------------------------------------------------------------------------
# Config (.env)
# ---------------------------------------------------------------------------
load_dotenv()

PORT: int = int(os.getenv("PORT", "5000"))
HOST: str = os.getenv("HOST", "0.0.0.0")
ANNOUNCED_IP: str = os.getenv("ANNOUNCED_IP", "127.0.0.1")
LISTEN_IP: str = os.getenv("LISTEN_IP", "0.0.0.0")
RTC_MIN_PORT: int = int(os.getenv("RTC_MIN_PORT", "10000"))
RTC_MAX_PORT: int = int(os.getenv("RTC_MAX_PORT", "59999"))
CORS_ORIGINS: List[str] = [
    o.strip() for o in os.getenv("CORS_ORIGINS", "http://localhost,http://localhost:3000,http://localhost:5173").split(",")
]
ICE_SERVERS: List[Dict] = json.loads(
    os.getenv("ICE_SERVERS", '[{"urls": "stun:stun.l.google.com:19302"}]')
)

# ---------------------------------------------------------------------------
# Embedded Node.js mediasoup worker script
# ---------------------------------------------------------------------------
_WORKER_JS = r"""
'use strict';
const mediasoup = require('mediasoup');

let worker = null;
let routers = new Map();
let transports = new Map();
let producers = new Map();
let consumers = new Map();
let dataProducers = new Map();
let dataConsumers = new Map();

const CODECS = [
  { kind: 'audio', mimeType: 'audio/opus',  clockRate: 48000, channels: 2,
    parameters: { minptime: 10, useinbandfec: 1, stereo: 1, 'sprop-stereo': 1 } },
  { kind: 'video', mimeType: 'video/VP8',   clockRate: 90000, parameters: {} },
  { kind: 'video', mimeType: 'video/H264',  clockRate: 90000,
    parameters: { 'packetization-mode': 1, 'profile-level-id': '640033' } },
];

function send(id, result, error) {
  const msg = error ? { id, error: String(error) } : { id, result };
  process.stdout.write(JSON.stringify(msg) + '\n');
}

const handlers = {
  async init({ rtcMinPort, rtcMaxPort }) {
    worker = await mediasoup.createWorker({
      logLevel: 'warn',
      rtcMinPort,
      rtcMaxPort,
    });
    worker.on('died', () => { process.stderr.write('worker died\n'); process.exit(1); });
    return { pid: worker.pid };
  },

  async createRouter({}) {
    const router = await worker.createRouter({ mediaCodecs: CODECS });
    routers.set(router.id, router);
    return { routerId: router.id, rtpCapabilities: router.rtpCapabilities };
  },

  async getRtpCapabilities({ routerId }) {
    const router = routers.get(routerId);
    if (!router) throw new Error('Router not found');
    return router.rtpCapabilities;
  },

  async createWebRtcTransport({ routerId, listenIp, announcedIp }) {
    const router = routers.get(routerId);
    if (!router) throw new Error('Router not found');
    const transport = await router.createWebRtcTransport({
      listenIps: [{ ip: listenIp, announcedIp }],
      enableUdp: true, enableTcp: true, preferUdp: true,
      enableSctp: true,
      numSctpStreams: { OS: 1024, MIS: 1024 },
      maxSctpMessageSize: 262144,
    });
    transports.set(transport.id, transport);
    return {
      transportId: transport.id,
      iceParameters: transport.iceParameters,
      iceCandidates: transport.iceCandidates,
      dtlsParameters: transport.dtlsParameters,
      sctpParameters: transport.sctpParameters,
    };
  },

  async connectTransport({ transportId, dtlsParameters }) {
    const transport = transports.get(transportId);
    if (!transport) throw new Error('Transport not found');
    await transport.connect({ dtlsParameters });
    return {};
  },

  async produce({ transportId, kind, rtpParameters, appData }) {
    const transport = transports.get(transportId);
    if (!transport) throw new Error('Transport not found');
    const producer = await transport.produce({ kind, rtpParameters, appData: appData || {} });
    producers.set(producer.id, producer);
    return { producerId: producer.id };
  },

  async consume({ transportId, producerId, rtpCapabilities }) {
    const transport = transports.get(transportId);
    const producer = producers.get(producerId);
    if (!transport) throw new Error('Transport not found');
    if (!producer) throw new Error('Producer not found');
    let router = null;
    for (const r of routers.values()) {
      if (r.canConsume({ producerId, rtpCapabilities })) { router = r; break; }
    }
    if (!router) throw new Error('Cannot consume: incompatible RTP capabilities');
    const consumer = await transport.consume({ producerId, rtpCapabilities, paused: true });
    consumers.set(consumer.id, consumer);
    return {
      consumerId: consumer.id,
      producerId,
      kind: consumer.kind,
      rtpParameters: consumer.rtpParameters,
    };
  },

  async resumeConsumer({ consumerId }) {
    const consumer = consumers.get(consumerId);
    if (!consumer) throw new Error('Consumer not found');
    await consumer.resume();
    return {};
  },

  async pauseProducer({ producerId }) {
    const producer = producers.get(producerId);
    if (!producer) throw new Error('Producer not found');
    await producer.pause();
    return {};
  },

  async resumeProducer({ producerId }) {
    const producer = producers.get(producerId);
    if (!producer) throw new Error('Producer not found');
    await producer.resume();
    return {};
  },

  async closeProducer({ producerId }) {
    const producer = producers.get(producerId);
    if (!producer) throw new Error('Producer not found');
    producer.close();
    producers.delete(producerId);
    return {};
  },

  async produceData({ transportId, label, protocol, sctpStreamParameters, appData }) {
    const transport = transports.get(transportId);
    if (!transport) throw new Error('Transport not found');
    const dp = await transport.produceData({
      label, protocol: protocol || 'webrtc-datachannel',
      sctpStreamParameters, appData: appData || {},
    });
    dataProducers.set(dp.id, dp);
    return { dataProducerId: dp.id };
  },

  async consumeData({ transportId, dataProducerId }) {
    const transport = transports.get(transportId);
    const dp = dataProducers.get(dataProducerId);
    if (!transport) throw new Error('Transport not found');
    if (!dp) throw new Error('DataProducer not found');
    const dc = await transport.consumeData({ dataProducerId });
    dataConsumers.set(dc.id, dc);
    return {
      dataConsumerId: dc.id,
      dataProducerId,
      label: dc.label,
      protocol: dc.protocol,
      sctpStreamParameters: dc.sctpStreamParameters,
    };
  },

  async closeTransport({ transportId }) {
    const transport = transports.get(transportId);
    if (transport) { transport.close(); transports.delete(transportId); }
    return {};
  },

  async closeRouter({ routerId }) {
    const router = routers.get(routerId);
    if (router) { router.close(); routers.delete(routerId); }
    return {};
  },
};

let buf = '';
process.stdin.setEncoding('utf8');
process.stdin.on('data', async (chunk) => {
  buf += chunk;
  const lines = buf.split('\n');
  buf = lines.pop();
  for (const line of lines) {
    if (!line.trim()) continue;
    let msg;
    try { msg = JSON.parse(line); } catch { continue; }
    const { id, method, params } = msg;
    try {
      const handler = handlers[method];
      if (!handler) throw new Error(`Unknown method: ${method}`);
      const result = await handler(params || {});
      send(id, result);
    } catch (e) {
      send(id, null, e.message || e);
    }
  }
});
process.stdin.on('end', () => process.exit(0));
"""

# ---------------------------------------------------------------------------
# In-memory data model
# ---------------------------------------------------------------------------
@dataclass
class PeerData:
    peer_id: str
    socket_id: str
    room_id: str
    display_name: str = ""
    transport_ids: List[str] = field(default_factory=list)
    producer_ids: List[str] = field(default_factory=list)
    consumer_ids: List[str] = field(default_factory=list)
    data_producer_ids: List[str] = field(default_factory=list)
    data_consumer_ids: List[str] = field(default_factory=list)


@dataclass
class TransportData:
    transport_id: str
    peer_id: str
    room_id: str
    direction: str  # "send" | "recv"


@dataclass
class ProducerData:
    producer_id: str
    peer_id: str
    room_id: str
    kind: str
    app_data: Dict = field(default_factory=dict)


@dataclass
class ConsumerData:
    consumer_id: str
    peer_id: str
    room_id: str
    producer_id: str
    kind: str


@dataclass
class RoomData:
    room_id: str
    router_id: str
    rtp_capabilities: Dict
    peers: Dict[str, PeerData] = field(default_factory=dict)
    transports: Dict[str, TransportData] = field(default_factory=dict)
    producers: Dict[str, ProducerData] = field(default_factory=dict)
    consumers: Dict[str, ConsumerData] = field(default_factory=dict)
    data_producers: Dict[str, Dict] = field(default_factory=dict)
    data_consumers: Dict[str, Dict] = field(default_factory=dict)


# ---------------------------------------------------------------------------
# Mediasoup Worker (Node.js subprocess, JSON-RPC over stdio)
# ---------------------------------------------------------------------------
class MediasoupWorker:
    def __init__(self) -> None:
        self._proc: Optional[asyncio.subprocess.Process] = None
        self._pending: Dict[str, asyncio.Future] = {}
        self._script_path: Optional[str] = None
        self._read_task: Optional[asyncio.Task] = None
        self._counter = 0

    async def start(self) -> None:
        fd, path = tempfile.mkstemp(suffix=".js", prefix="ms_worker_")
        with os.fdopen(fd, "w") as f:
            f.write(_WORKER_JS)
        self._script_path = path

        project_dir = os.path.dirname(os.path.abspath(__file__))
        node_modules = os.path.join(project_dir, "node_modules")
        env = os.environ.copy()
        existing = env.get("NODE_PATH", "")
        env["NODE_PATH"] = node_modules if not existing else f"{node_modules}{os.pathsep}{existing}"

        self._proc = await asyncio.create_subprocess_exec(
            "node", path,
            stdin=asyncio.subprocess.PIPE,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
            env=env,
        )
        self._read_task = asyncio.ensure_future(self._read_loop())
        result = await self.call("init", {"rtcMinPort": RTC_MIN_PORT, "rtcMaxPort": RTC_MAX_PORT})
        log.info("Mediasoup worker started (Node.js PID %s)", result.get("pid"))

    async def _read_loop(self) -> None:
        assert self._proc and self._proc.stdout
        async for raw_line in self._proc.stdout:
            line = raw_line.decode().strip()
            if not line:
                continue
            try:
                msg = json.loads(line)
            except json.JSONDecodeError:
                log.warning("Worker non-JSON: %s", line)
                continue
            rpc_id = msg.get("id")
            fut = self._pending.pop(rpc_id, None)
            if fut is None or fut.done():
                continue
            if "error" in msg:
                fut.set_exception(RuntimeError(msg["error"]))
            else:
                fut.set_result(msg.get("result", {}))
        assert self._proc.stderr
        stderr = await self._proc.stderr.read()
        if stderr:
            log.error("Worker stderr: %s", stderr.decode())

    async def call(self, method: str, params: Dict) -> Dict:
        assert self._proc and self._proc.stdin, "Worker not started"
        self._counter += 1
        rpc_id = str(self._counter)
        fut: asyncio.Future = asyncio.get_event_loop().create_future()
        self._pending[rpc_id] = fut
        msg = json.dumps({"id": rpc_id, "method": method, "params": params}) + "\n"
        self._proc.stdin.write(msg.encode())
        await self._proc.stdin.drain()
        return await asyncio.wait_for(fut, timeout=10.0)

    async def stop(self) -> None:
        if self._proc:
            try:
                self._proc.stdin.close()
                await self._proc.wait()
            except Exception:
                pass
        if self._script_path and os.path.exists(self._script_path):
            os.unlink(self._script_path)


_worker: Optional[MediasoupWorker] = None
_worker_available = False


async def _ensure_worker() -> Optional[MediasoupWorker]:
    if _worker_available:
        return _worker
    return None


# ---------------------------------------------------------------------------
# Room Manager
# ---------------------------------------------------------------------------
class RoomManager:
    def __init__(self) -> None:
        self.rooms: Dict[str, RoomData] = {}
        self._socket_map: Dict[str, tuple[str, str]] = {}

    async def get_or_create_room(self, room_id: str) -> RoomData:
        if room_id in self.rooms:
            return self.rooms[room_id]
        worker = await _ensure_worker()
        if worker:
            data = await worker.call("createRouter", {})
            router_id = data["routerId"]
            rtp_caps = data["rtpCapabilities"]
        else:
            router_id = f"router-{uuid.uuid4().hex[:8]}"
            rtp_caps = {}
            log.warning("Mediasoup worker unavailable – signaling-only mode for room %s", room_id)
        room = RoomData(room_id=room_id, router_id=router_id, rtp_capabilities=rtp_caps)
        self.rooms[room_id] = room
        log.info("Room created: %s (router %s)", room_id, router_id)
        return room

    def add_peer(self, room: RoomData, socket_id: str, peer_id: str, display_name: str) -> PeerData:
        peer = PeerData(peer_id=peer_id, socket_id=socket_id, room_id=room.room_id, display_name=display_name)
        room.peers[peer_id] = peer
        self._socket_map[socket_id] = (room.room_id, peer_id)
        return peer

    def get_peer_by_socket(self, socket_id: str) -> Optional[tuple[RoomData, PeerData]]:
        entry = self._socket_map.get(socket_id)
        if not entry:
            return None
        room_id, peer_id = entry
        room = self.rooms.get(room_id)
        if not room:
            return None
        peer = room.peers.get(peer_id)
        if not peer:
            return None
        return room, peer

    async def remove_peer(self, socket_id: str) -> Optional[tuple[RoomData, PeerData]]:
        entry = self._socket_map.pop(socket_id, None)
        if not entry:
            return None
        room_id, peer_id = entry
        room = self.rooms.get(room_id)
        if not room:
            return None
        peer = room.peers.pop(peer_id, None)
        if not peer:
            return None
        worker = await _ensure_worker()
        if worker:
            for tid in list(peer.transport_ids):
                try:
                    await worker.call("closeTransport", {"transportId": tid})
                except Exception as e:
                    log.debug("closeTransport %s: %s", tid, e)
                room.transports.pop(tid, None)
        for pid in peer.producer_ids:
            room.producers.pop(pid, None)
        for cid in peer.consumer_ids:
            room.consumers.pop(cid, None)
        for dpid in peer.data_producer_ids:
            room.data_producers.pop(dpid, None)
        for dcid in peer.data_consumer_ids:
            room.data_consumers.pop(dcid, None)
        if not room.peers:
            if worker and room.router_id:
                try:
                    await worker.call("closeRouter", {"routerId": room.router_id})
                except Exception as e:
                    log.debug("closeRouter %s: %s", room.router_id, e)
            del self.rooms[room_id]
            log.info("Room %s closed (empty)", room_id)
        return room, peer


_rooms = RoomManager()


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
def _ok(data: Any = None) -> Dict:
    return {"ok": True, **(data if isinstance(data, dict) else {})}


def _err(msg: str) -> Dict:
    return {"ok": False, "error": msg}


# ---------------------------------------------------------------------------
# Socket.IO server
# ---------------------------------------------------------------------------
sio = socketio.AsyncServer(
    async_mode="asgi",
    cors_allowed_origins=CORS_ORIGINS,
    logger=False,
    engineio_logger=False,
)


# ── join-room ────────────────────────────────────────────────────────────────
@sio.on("join-room")
async def join_room(sid: str, data: Dict):
    room_id = data.get("roomId", "").strip()
    peer_id = data.get("peerId") or uuid.uuid4().hex
    display_name = data.get("displayName", "")

    if not room_id:
        return _err("roomId required")

    room = await _rooms.get_or_create_room(room_id)

    if peer_id in room.peers:
        return _err("peerId already in room")

    peer = _rooms.add_peer(room, sid, peer_id, display_name)
    await sio.enter_room(sid, room_id)

    existing_peers = [
        {"peerId": p.peer_id, "displayName": p.display_name}
        for p in room.peers.values()
        if p.peer_id != peer_id
    ]
    await sio.emit(
        "peer-joined",
        {"peerId": peer_id, "displayName": display_name},
        room=room_id,
        skip_sid=sid,
    )

    log.info("Peer %s joined room %s (%d peers total)", peer_id, room_id, len(room.peers))
    return _ok({
        "peerId": peer_id,
        "rtpCapabilities": room.rtp_capabilities,
        "peers": existing_peers,
    })


# ── leave-room ───────────────────────────────────────────────────────────────
@sio.on("leave-room")
async def leave_room(sid: str, data: Dict = None):
    result = await _rooms.remove_peer(sid)
    if not result:
        return _err("not in a room")
    room, peer = result
    await sio.leave_room(sid, room.room_id)
    await sio.emit("peer-left", {"peerId": peer.peer_id}, room=room.room_id)
    log.info("Peer %s left room %s", peer.peer_id, room.room_id)
    return _ok()


# ── get-rtp-capabilities ──────────────────────────────────────────────────────
@sio.on("get-rtp-capabilities")
async def get_rtp_capabilities(sid: str, data: Dict = None):
    result = _rooms.get_peer_by_socket(sid)
    if not result:
        return _err("not in a room")
    room, _ = result
    return _ok({"rtpCapabilities": room.rtp_capabilities})


# ── get-ice-servers ──────────────────────────────────────────────────────────
@sio.on("get-ice-servers")
async def get_ice_servers(sid: str, data: Dict = None):
    return _ok({"iceServers": ICE_SERVERS})


# ── create-transport ─────────────────────────────────────────────────────────
@sio.on("create-transport")
async def create_transport(sid: str, data: Dict):
    direction = data.get("direction", "send")
    result = _rooms.get_peer_by_socket(sid)
    if not result:
        return _err("not in a room")
    room, peer = result

    worker = await _ensure_worker()
    if not worker:
        return _err("SFU worker not available")

    try:
        t = await worker.call("createWebRtcTransport", {
            "routerId": room.router_id,
            "listenIp": LISTEN_IP,
            "announcedIp": ANNOUNCED_IP,
        })
    except Exception as e:
        log.error("createWebRtcTransport: %s", e)
        return _err(str(e))

    transport_id = t["transportId"]
    room.transports[transport_id] = TransportData(
        transport_id=transport_id,
        peer_id=peer.peer_id,
        room_id=room.room_id,
        direction=direction,
    )
    peer.transport_ids.append(transport_id)

    return _ok({
        "id": transport_id,
        "transportId": transport_id,
        "iceParameters": t["iceParameters"],
        "iceCandidates": t["iceCandidates"],
        "dtlsParameters": t["dtlsParameters"],
        "sctpParameters": t.get("sctpParameters"),
    })


# ── connect-transport ─────────────────────────────────────────────────────────
@sio.on("connect-transport")
async def connect_transport(sid: str, data: Dict):
    transport_id = data.get("transportId")
    dtls_parameters = data.get("dtlsParameters")

    result = _rooms.get_peer_by_socket(sid)
    if not result:
        return _err("not in a room")
    room, peer = result

    if transport_id not in room.transports:
        return _err("transport not found")

    worker = await _ensure_worker()
    if not worker:
        return _err("SFU worker not available")

    try:
        await worker.call("connectTransport", {
            "transportId": transport_id,
            "dtlsParameters": dtls_parameters,
        })
    except Exception as e:
        log.error("connectTransport: %s", e)
        return _err(str(e))

    return _ok()


# ── produce ───────────────────────────────────────────────────────────────────
@sio.on("produce")
async def produce(sid: str, data: Dict):
    transport_id = data.get("transportId")
    kind = data.get("kind")
    rtp_parameters = data.get("rtpParameters")
    app_data = data.get("appData", {})

    result = _rooms.get_peer_by_socket(sid)
    if not result:
        return _err("not in a room")
    room, peer = result

    if transport_id not in room.transports:
        return _err("transport not found")

    worker = await _ensure_worker()
    if not worker:
        return _err("SFU worker not available")

    try:
        p = await worker.call("produce", {
            "transportId": transport_id,
            "kind": kind,
            "rtpParameters": rtp_parameters,
            "appData": app_data,
        })
    except Exception as e:
        log.error("produce: %s", e)
        return _err(str(e))

    producer_id = p["producerId"]
    room.producers[producer_id] = ProducerData(
        producer_id=producer_id,
        peer_id=peer.peer_id,
        room_id=room.room_id,
        kind=kind,
        app_data=app_data,
    )
    peer.producer_ids.append(producer_id)

    await sio.emit(
        "new-producer",
        {"producerId": producer_id, "peerId": peer.peer_id, "kind": kind, "appData": app_data},
        room=room.room_id,
        skip_sid=sid,
    )

    return _ok({"producerId": producer_id})


# ── consume ───────────────────────────────────────────────────────────────────
@sio.on("consume")
async def consume(sid: str, data: Dict):
    transport_id = data.get("transportId")
    producer_id = data.get("producerId")
    rtp_capabilities = data.get("rtpCapabilities")

    result = _rooms.get_peer_by_socket(sid)
    if not result:
        return _err("not in a room")
    room, peer = result

    if transport_id not in room.transports:
        return _err("transport not found")
    if producer_id not in room.producers:
        return _err("producer not found")

    worker = await _ensure_worker()
    if not worker:
        return _err("SFU worker not available")

    try:
        c = await worker.call("consume", {
            "transportId": transport_id,
            "producerId": producer_id,
            "rtpCapabilities": rtp_capabilities,
        })
    except Exception as e:
        log.error("consume: %s", e)
        return _err(str(e))

    consumer_id = c["consumerId"]
    room.consumers[consumer_id] = ConsumerData(
        consumer_id=consumer_id,
        peer_id=peer.peer_id,
        room_id=room.room_id,
        producer_id=producer_id,
        kind=c["kind"],
    )
    peer.consumer_ids.append(consumer_id)

    return _ok({
        "consumerId": consumer_id,
        "producerId": producer_id,
        "kind": c["kind"],
        "rtpParameters": c["rtpParameters"],
    })


# ── resume-consumer ───────────────────────────────────────────────────────────
@sio.on("resume-consumer")
async def resume_consumer(sid: str, data: Dict):
    consumer_id = data.get("consumerId")
    result = _rooms.get_peer_by_socket(sid)
    if not result:
        return _err("not in a room")
    room, peer = result

    if consumer_id not in room.consumers:
        return _err("consumer not found")

    worker = await _ensure_worker()
    if not worker:
        return _err("SFU worker not available")

    try:
        await worker.call("resumeConsumer", {"consumerId": consumer_id})
    except Exception as e:
        return _err(str(e))
    return _ok()


# ── pause-producer ────────────────────────────────────────────────────────────
@sio.on("pause-producer")
async def pause_producer(sid: str, data: Dict):
    producer_id = data.get("producerId")
    result = _rooms.get_peer_by_socket(sid)
    if not result:
        return _err("not in a room")
    room, peer = result

    if producer_id not in room.producers:
        return _err("producer not found")

    worker = await _ensure_worker()
    if not worker:
        return _err("SFU worker not available")

    try:
        await worker.call("pauseProducer", {"producerId": producer_id})
    except Exception as e:
        return _err(str(e))
    return _ok()


# ── resume-producer ───────────────────────────────────────────────────────────
@sio.on("resume-producer")
async def resume_producer(sid: str, data: Dict):
    producer_id = data.get("producerId")
    result = _rooms.get_peer_by_socket(sid)
    if not result:
        return _err("not in a room")
    room, peer = result

    if producer_id not in room.producers:
        return _err("producer not found")

    worker = await _ensure_worker()
    if not worker:
        return _err("SFU worker not available")

    try:
        await worker.call("resumeProducer", {"producerId": producer_id})
    except Exception as e:
        return _err(str(e))
    return _ok()


# ── close-producer ────────────────────────────────────────────────────────────
@sio.on("close-producer")
async def close_producer(sid: str, data: Dict):
    producer_id = data.get("producerId")
    result = _rooms.get_peer_by_socket(sid)
    if not result:
        return _err("not in a room")
    room, peer = result

    if producer_id not in room.producers:
        return _err("producer not found")

    worker = await _ensure_worker()
    if worker:
        try:
            await worker.call("closeProducer", {"producerId": producer_id})
        except Exception as e:
            log.debug("closeProducer: %s", e)

    room.producers.pop(producer_id, None)
    if producer_id in peer.producer_ids:
        peer.producer_ids.remove(producer_id)

    await sio.emit(
        "producer-closed",
        {"producerId": producer_id, "peerId": peer.peer_id},
        room=room.room_id,
        skip_sid=sid,
    )
    return _ok()


# ── produce-data ──────────────────────────────────────────────────────────────
@sio.on("produce-data")
async def produce_data(sid: str, data: Dict):
    transport_id = data.get("transportId")
    label = data.get("label", "data")
    protocol = data.get("protocol", "webrtc-datachannel")
    sctp_params = data.get("sctpStreamParameters")
    app_data = data.get("appData", {})

    result = _rooms.get_peer_by_socket(sid)
    if not result:
        return _err("not in a room")
    room, peer = result

    if transport_id not in room.transports:
        return _err("transport not found")

    worker = await _ensure_worker()
    if not worker:
        return _err("SFU worker not available")

    try:
        dp = await worker.call("produceData", {
            "transportId": transport_id,
            "label": label,
            "protocol": protocol,
            "sctpStreamParameters": sctp_params,
            "appData": app_data,
        })
    except Exception as e:
        return _err(str(e))

    dp_id = dp["dataProducerId"]
    room.data_producers[dp_id] = {"dataProducerId": dp_id, "peerId": peer.peer_id, "label": label}
    peer.data_producer_ids.append(dp_id)
    return _ok({"dataProducerId": dp_id})


# ── consume-data ──────────────────────────────────────────────────────────────
@sio.on("consume-data")
async def consume_data(sid: str, data: Dict):
    transport_id = data.get("transportId")
    data_producer_id = data.get("dataProducerId")

    result = _rooms.get_peer_by_socket(sid)
    if not result:
        return _err("not in a room")
    room, peer = result

    if transport_id not in room.transports:
        return _err("transport not found")
    if data_producer_id not in room.data_producers:
        return _err("dataProducer not found")

    worker = await _ensure_worker()
    if not worker:
        return _err("SFU worker not available")

    try:
        dc = await worker.call("consumeData", {
            "transportId": transport_id,
            "dataProducerId": data_producer_id,
        })
    except Exception as e:
        return _err(str(e))

    dc_id = dc["dataConsumerId"]
    room.data_consumers[dc_id] = {**dc, "peerId": peer.peer_id}
    peer.data_consumer_ids.append(dc_id)

    return _ok({
        "dataConsumerId": dc_id,
        "dataProducerId": data_producer_id,
        "label": dc.get("label"),
        "protocol": dc.get("protocol"),
        "sctpStreamParameters": dc.get("sctpStreamParameters"),
    })


# ── stream-ready ──────────────────────────────────────────────────────────────
@sio.on("stream-ready")
async def stream_ready(sid: str, data: Dict):
    result = _rooms.get_peer_by_socket(sid)
    if not result:
        return _err("not in a room")
    room, peer = result

    await sio.emit(
        "stream-ready",
        {"peerId": peer.peer_id, **(data or {})},
        room=room.room_id,
        skip_sid=sid,
    )
    return _ok()


# ── video-packet ──────────────────────────────────────────────────────────────
@sio.on("video-packet")
async def video_packet(sid: str, data: Dict):
    result = _rooms.get_peer_by_socket(sid)
    if not result:
        return _err("not in a room")
    room, peer = result

    await sio.emit(
        "video-packet",
        {**data, "fromPeerId": peer.peer_id},
        room=room.room_id,
        skip_sid=sid,
    )
    return _ok()


# ── control ───────────────────────────────────────────────────────────────────
@sio.on("control")
async def control(sid: str, data: Dict):
    result = _rooms.get_peer_by_socket(sid)
    if not result:
        return _err("not in a room")
    room, peer = result

    target_peer_id = data.get("targetPeerId")
    payload = {**data, "fromPeerId": peer.peer_id}

    if target_peer_id:
        target = room.peers.get(target_peer_id)
        if target:
            await sio.emit("control", payload, to=target.socket_id)
    else:
        await sio.emit("control", payload, room=room.room_id, skip_sid=sid)

    return _ok()


# ── keyboard ──────────────────────────────────────────────────────────────────
@sio.on("keyboard")
async def keyboard(sid: str, data: Dict):
    result = _rooms.get_peer_by_socket(sid)
    if not result:
        return _err("not in a room")
    room, peer = result

    target_peer_id = data.get("targetPeerId")
    payload = {**data, "fromPeerId": peer.peer_id}

    if target_peer_id:
        target = room.peers.get(target_peer_id)
        if target:
            await sio.emit("keyboard", payload, to=target.socket_id)
    else:
        await sio.emit("keyboard", payload, room=room.room_id, skip_sid=sid)

    return _ok()


# ── mouse ─────────────────────────────────────────────────────────────────────
@sio.on("mouse")
async def mouse(sid: str, data: Dict):
    result = _rooms.get_peer_by_socket(sid)
    if not result:
        return _err("not in a room")
    room, peer = result

    target_peer_id = data.get("targetPeerId")
    payload = {**data, "fromPeerId": peer.peer_id}

    if target_peer_id:
        target = room.peers.get(target_peer_id)
        if target:
            await sio.emit("mouse", payload, to=target.socket_id)
    else:
        await sio.emit("mouse", payload, room=room.room_id, skip_sid=sid)

    return _ok()


# ── orientation-change ────────────────────────────────────────────────────────
@sio.on("orientation-change")
async def orientation_change(sid: str, data: Dict):
    result = _rooms.get_peer_by_socket(sid)
    if not result:
        return _err("not in a room")
    room, peer = result

    await sio.emit(
        "orientation-change",
        {**data, "fromPeerId": peer.peer_id},
        room=room.room_id,
        skip_sid=sid,
    )
    return _ok()


# ── kick-peer ─────────────────────────────────────────────────────────────────
@sio.on("kick-peer")
async def kick_peer(sid: str, data: Dict):
    target_peer_id = data.get("peerId")
    result = _rooms.get_peer_by_socket(sid)
    if not result:
        return _err("not in a room")
    room, peer = result

    target = room.peers.get(target_peer_id)
    if not target:
        return _err("target peer not found")

    await sio.emit("kicked", {"by": peer.peer_id}, to=target.socket_id)

    removed = await _rooms.remove_peer(target.socket_id)
    if removed:
        r, kicked_peer = removed
        await sio.leave_room(target.socket_id, r.room_id)
        await sio.emit("peer-left", {"peerId": kicked_peer.peer_id}, room=r.room_id)

    log.info("Peer %s kicked %s from room %s", peer.peer_id, target_peer_id, room.room_id)
    return _ok()


# ── disconnect ────────────────────────────────────────────────────────────────
@sio.event
async def disconnect(sid: str):
    result = await _rooms.remove_peer(sid)
    if result:
        room, peer = result
        await sio.emit("peer-left", {"peerId": peer.peer_id}, room=room.room_id)
        log.info("Peer %s disconnected from room %s", peer.peer_id, room.room_id)


# ---------------------------------------------------------------------------
# FastAPI app
# ---------------------------------------------------------------------------
app = FastAPI(title="Remote Device Control – Signaling Server")

app.add_middleware(
    CORSMiddleware,
    allow_origins=CORS_ORIGINS,
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)


@app.get("/health")
async def health():
    return {
        "status": "ok",
        "rooms": len(_rooms.rooms),
        "peers": sum(len(r.peers) for r in _rooms.rooms.values()),
        "sfuWorker": _worker_available,
    }


@app.get("/rooms")
async def list_rooms():
    return {
        room_id: {
            "peers": [
                {"peerId": p.peer_id, "displayName": p.display_name}
                for p in room.peers.values()
            ]
        }
        for room_id, room in _rooms.rooms.items()
    }


# Mount Socket.IO on top of FastAPI
socket_app = socketio.ASGIApp(sio, other_asgi_app=app)


# ---------------------------------------------------------------------------
# Startup / shutdown lifecycle
# ---------------------------------------------------------------------------
async def _on_startup():
    global _worker, _worker_available
    try:
        _worker = MediasoupWorker()
        await _worker.start()
        _worker_available = True
        log.info("Mediasoup worker ready")
    except FileNotFoundError:
        log.warning("'node' not found in PATH – running in signaling-only mode (no SFU).")
        _worker_available = False
    except Exception as e:
        log.warning("Mediasoup worker failed to start: %s – signaling-only mode.", e)
        _worker_available = False


async def _on_shutdown():
    if _worker:
        await _worker.stop()
        log.info("Mediasoup worker stopped")


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------
if __name__ == "__main__":
    import signal

    loop = asyncio.new_event_loop()
    asyncio.set_event_loop(loop)

    loop.run_until_complete(_on_startup())

    config = uvicorn.Config(
        app=socket_app,
        host=HOST,
        port=PORT,
        loop="asyncio",
        log_level="info",
    )
    server = uvicorn.Server(config)

    original_handle_exit = server.handle_exit

    def handle_exit(sig, frame):
        loop.run_until_complete(_on_shutdown())
        original_handle_exit(sig, frame)

    for s in (signal.SIGINT, signal.SIGTERM):
        try:
            signal.signal(s, handle_exit)
        except (OSError, ValueError):
            pass

    try:
        loop.run_until_complete(server.serve())
    finally:
        loop.run_until_complete(_on_shutdown())
        loop.close()
