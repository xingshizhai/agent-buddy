# daemon/config.py
"""Load daemon configuration from ~/.config/agent-buddy/config.toml.

Falls back to built-in defaults when the file is absent or a key is missing.
"""
import logging
import tomllib
from dataclasses import dataclass, field
from pathlib import Path

log = logging.getLogger(__name__)

CONFIG_FILE = Path.home() / ".config" / "agent-buddy" / "config.toml"


@dataclass
class ProxyConfig:
    url: str = ""       # e.g. "http://127.0.0.1:7890" — empty = no proxy


@dataclass
class BleConfig:
    scan_timeout: int  = 10   # seconds per discovery scan
    reconnect_wait: int = 5   # seconds between reconnect attempts
    max_backoff: int   = 60   # maximum retry backoff in seconds


@dataclass
class ClaudeConfig:
    poll_interval: int   = 60   # seconds between API polls
    credentials_file: str = ""  # override path; empty = ~/.claude/.credentials.json


@dataclass
class KimiConfig:
    poll_interval: int = 300   # seconds between API polls (5 min default)
    auth_token: str    = ""    # kimi-auth JWT; empty = read from ~/.config/agent-buddy/kimi-auth.txt
    enabled: bool      = False # must be explicitly enabled


@dataclass
class Config:
    proxy:  ProxyConfig  = field(default_factory=ProxyConfig)
    ble:    BleConfig    = field(default_factory=BleConfig)
    claude: ClaudeConfig = field(default_factory=ClaudeConfig)
    kimi:   KimiConfig   = field(default_factory=KimiConfig)


def load(path: Path | None = None) -> Config:
    """Load config from *path* (defaults to CONFIG_FILE).

    Returns a Config with defaults if the file is missing or unreadable.
    """
    cfg_path = path or CONFIG_FILE
    cfg = Config()

    if not cfg_path.exists():
        log.debug("No config file at %s — using defaults", cfg_path)
        return cfg

    try:
        with open(cfg_path, "rb") as f:
            data = tomllib.load(f)
    except Exception as e:
        log.warning("Cannot read config %s: %s — using defaults", cfg_path, e)
        return cfg

    if proxy := data.get("proxy", {}):
        cfg.proxy.url = proxy.get("url", "")

    if ble := data.get("ble", {}):
        cfg.ble.scan_timeout  = int(ble.get("scan_timeout",  cfg.ble.scan_timeout))
        cfg.ble.reconnect_wait = int(ble.get("reconnect_wait", cfg.ble.reconnect_wait))
        cfg.ble.max_backoff   = int(ble.get("max_backoff",   cfg.ble.max_backoff))

    services = data.get("services", {})

    if claude := services.get("claude", {}):
        cfg.claude.poll_interval    = int(claude.get("poll_interval",    cfg.claude.poll_interval))
        cfg.claude.credentials_file = claude.get("credentials_file", "")

    if kimi := services.get("kimi", {}):
        cfg.kimi.enabled       = bool(kimi.get("enabled", cfg.kimi.enabled))
        cfg.kimi.poll_interval = int(kimi.get("poll_interval", cfg.kimi.poll_interval))
        cfg.kimi.auth_token    = kimi.get("auth_token", "")

    log.info("Config loaded from %s", cfg_path)
    return cfg
