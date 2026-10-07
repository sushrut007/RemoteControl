"""WebSocket integration tests."""

import json

from fastapi.testclient import TestClient

from server import Settings, create_app


def _settings() -> Settings:
    return Settings(
        host="127.0.0.1",
        port=8765,
        tls_enabled=False,
        ssl_certfile=None,
        ssl_keyfile=None,
        max_room_size=5,
        rate_limit_per_sec=100,
        log_level="WARNING",
        max_message_bytes=65536,
        max_sdp_bytes=262144,
    )


def test_two_clients_relay_fake_sdp() -> None:
    app = create_app(_settings())
    fake_sdp = "v=0\r\no=fake\r\n"

    with TestClient(app) as client:
        with client.websocket_connect("/ws") as host_ws:
            with client.websocket_connect("/ws") as peer_ws:
                host_ws.send_json({"v": 1, "type": "hello", "device_name": "Host"})
                host_hello = host_ws.receive_json()
                assert host_hello["type"] == "hello_ok"
                host_id = host_hello["device_id"]

                peer_ws.send_json({"v": 1, "type": "hello", "device_name": "Peer"})
                peer_hello = peer_ws.receive_json()
                peer_id = peer_hello["device_id"]

                host_ws.send_json({"v": 1, "type": "create_room"})
                created = host_ws.receive_json()
                assert created["type"] == "room_created"
                room_id = created["room_id"]

                peer_ws.send_json({"v": 1, "type": "join_room", "room_id": room_id})
                joined = peer_ws.receive_json()
                assert joined["type"] == "room_joined"

                host_ws.send_json(
                    {
                        "v": 1,
                        "type": "signal",
                        "signal_type": "offer",
                        "target_device_id": peer_id,
                        "sdp": fake_sdp,
                    }
                )

                while True:
                    msg = peer_ws.receive_json()
                    if msg.get("type") == "signal":
                        break
                assert msg["signal_type"] == "offer"
                assert msg["sdp"] == fake_sdp
                assert msg["from_device_id"] == host_id


def test_forbidden_media_type_rejected() -> None:
    app = create_app(_settings())
    with TestClient(app) as client:
        with client.websocket_connect("/ws") as ws:
            ws.send_json({"v": 1, "type": "hello", "device_name": "X"})
            ws.receive_json()
            ws.send_json({"v": 1, "type": "video", "data": "nope"})
            err = ws.receive_json()
            assert err["type"] == "error"
            assert err["code"] == "FORBIDDEN"
