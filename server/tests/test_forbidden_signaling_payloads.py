"""Ensure signaling server rejects media/file payload message types."""

import pytest
from fastapi.testclient import TestClient

from server import FORBIDDEN, create_app
from tests.test_ws_integration import _settings

FORBIDDEN_TYPES = [
    "video",
    "audio",
    "file",
    "file_chunk",
    "input",
    "input_event",
    "clipboard",
    "screen_frame",
]


@pytest.mark.parametrize("msg_type", FORBIDDEN_TYPES)
def test_forbidden_types_rejected(msg_type: str) -> None:
    app = create_app(_settings())
    with TestClient(app) as client:
        with client.websocket_connect("/ws") as ws:
            ws.send_json({"v": 1, "type": "hello", "device_name": "Audit"})
            ws.receive_json()
            ws.send_json({"v": 1, "type": msg_type, "payload": "must-not-relay"})
            err = ws.receive_json()
            assert err["type"] == "error"
            assert err["code"] == "FORBIDDEN"
