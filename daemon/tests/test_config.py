# daemon/tests/test_config.py
import pytest
from daemon.config import load, Config


def test_defaults_when_no_file(tmp_path):
    cfg = load(tmp_path / "missing.toml")
    assert cfg.proxy.url == ""
    assert cfg.ble.scan_timeout == 10
    assert cfg.ble.reconnect_wait == 5
    assert cfg.ble.max_backoff == 60
    assert cfg.claude.poll_interval == 60
    assert cfg.claude.credentials_file == ""


def test_proxy_url(tmp_path):
    f = tmp_path / "config.toml"
    f.write_text('[proxy]\nurl = "http://127.0.0.1:7890"\n')
    cfg = load(f)
    assert cfg.proxy.url == "http://127.0.0.1:7890"


def test_ble_settings(tmp_path):
    f = tmp_path / "config.toml"
    f.write_text("[ble]\nscan_timeout = 15\nreconnect_wait = 10\nmax_backoff = 120\n")
    cfg = load(f)
    assert cfg.ble.scan_timeout == 15
    assert cfg.ble.reconnect_wait == 10
    assert cfg.ble.max_backoff == 120


def test_claude_settings(tmp_path):
    f = tmp_path / "config.toml"
    f.write_text('[services.claude]\npoll_interval = 120\ncredentials_file = "/tmp/creds.json"\n')
    cfg = load(f)
    assert cfg.claude.poll_interval == 120
    assert cfg.claude.credentials_file == "/tmp/creds.json"


def test_partial_config_uses_defaults(tmp_path):
    f = tmp_path / "config.toml"
    f.write_text('[proxy]\nurl = "socks5://127.0.0.1:1080"\n')
    cfg = load(f)
    assert cfg.proxy.url == "socks5://127.0.0.1:1080"
    assert cfg.ble.scan_timeout == 10    # default
    assert cfg.claude.poll_interval == 60  # default


def test_invalid_toml_returns_defaults(tmp_path):
    f = tmp_path / "config.toml"
    f.write_text("this is not valid toml ][[[")
    cfg = load(f)
    assert cfg.proxy.url == ""           # falls back to defaults
