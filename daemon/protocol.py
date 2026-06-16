# daemon/protocol.py
import json

PROTOCOL_VERSION = 1


def make_data(svc: str, payload: dict) -> bytes:
    """Serialise a data message for writing to the ESP32 RX characteristic."""
    msg = {
        "type": "data",
        "v": PROTOCOL_VERSION,
        "svc": svc,
        "payload": payload,
    }
    return json.dumps(msg, separators=(",", ":")).encode()


def parse(raw: bytes) -> dict:
    """Deserialise a message received from the ESP32 TX or REQ characteristic."""
    return json.loads(raw.decode())
