# daemon/tests/test_protocol.py
import json
import pytest
from daemon.protocol import make_data, parse


def test_make_data_type_and_version():
    raw = make_data("claude", {"s": 42})
    msg = json.loads(raw)
    assert msg["type"] == "data"
    assert msg["v"] == 1


def test_make_data_svc_and_payload():
    raw = make_data("claude", {"s": 42, "sr": 180, "w": 17, "wr": 6230, "st": "allowed"})
    msg = json.loads(raw)
    assert msg["svc"] == "claude"
    assert msg["payload"]["s"] == 42
    assert msg["payload"]["st"] == "allowed"


def test_make_data_fits_in_512_bytes():
    raw = make_data("claude", {"s": 100, "sr": 300, "w": 50, "wr": 10080, "st": "allowed"})
    assert len(raw) <= 512


def test_make_data_returns_bytes():
    raw = make_data("claude", {})
    assert isinstance(raw, bytes)


def test_parse_cap_message():
    raw = b'{"type":"cap","v":1,"svcs":["claude"],"screens":["usage","ble"]}'
    msg = parse(raw)
    assert msg["type"] == "cap"
    assert msg["svcs"] == ["claude"]
    assert msg["screens"] == ["usage", "ble"]


def test_parse_ack_true():
    msg = parse(b'{"type":"ack","v":1,"ok":true}')
    assert msg["type"] == "ack"
    assert msg["ok"] is True


def test_parse_ack_false():
    msg = parse(b'{"type":"ack","v":1,"ok":false}')
    assert msg["ok"] is False


def test_parse_err():
    msg = parse(b'{"type":"err","v":1,"code":2,"msg":"unsupported svc"}')
    assert msg["type"] == "err"
    assert msg["code"] == 2
    assert msg["msg"] == "unsupported svc"


def test_parse_req_with_svc():
    msg = parse(b'{"type":"req","v":1,"svc":"claude"}')
    assert msg["type"] == "req"
    assert msg["svc"] == "claude"


def test_parse_req_without_svc():
    msg = parse(b'{"type":"req","v":1}')
    assert msg["type"] == "req"
    assert "svc" not in msg
