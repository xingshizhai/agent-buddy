# daemon/services/kimi.py
"""Kimi Code usage poller.

Calls BillingService/GetUsages on https://www.kimi.com using the kimi-auth JWT.
All required x-msh-* headers are derived from the JWT payload, so only the
token itself needs to be configured.

Data source: https://www.kimi.com/code/console (Kimi Code Console)
"""
import asyncio
import base64
import json
import logging
import subprocess
import time
from datetime import datetime, timezone
from pathlib import Path

from .base import ServiceBase

log = logging.getLogger(__name__)

_USAGES_URL  = "https://www.kimi.com/apiv2/kimi.gateway.billing.v1.BillingService/GetUsages"
_USAGES_BODY = '{"scope":["FEATURE_CODING"]}'

DEFAULT_TOKEN_FILE = Path.home() / ".config" / "agent-buddy" / "kimi-auth.txt"


def _parse_jwt_claims(token: str) -> dict:
    payload = token.split(".")[1]
    padded  = payload + "=" * (4 - len(payload) % 4)
    return json.loads(base64.urlsafe_b64decode(padded))


def _iso_to_reset_mins(iso_str: str, now: int) -> int:
    try:
        ts = datetime.fromisoformat(iso_str.replace("Z", "+00:00")).timestamp()
        return max(0, round((ts - now) / 60))
    except Exception:
        return -1


class KimiService(ServiceBase):
    service_id = "kimi"

    def __init__(self, auth_token: str = "", poll_interval: int = 300) -> None:
        self.poll_interval = poll_interval
        self._token = auth_token.strip() or self._load_token()

    def _load_token(self) -> str:
        try:
            return DEFAULT_TOKEN_FILE.read_text().strip()
        except OSError:
            return ""

    def _build_headers(self) -> dict:
        """Derive all required headers from the JWT claims."""
        try:
            claims = _parse_jwt_claims(self._token)
        except Exception as e:
            raise ValueError(f"Cannot parse kimi-auth JWT: {e}")
        return {
            "Authorization":            f"Bearer {self._token}",
            "x-msh-session-id":         claims.get("ssid", ""),
            "x-msh-platform":           "web",
            "x-msh-device-id":          claims.get("device_id", ""),
            "x-msh-version":            "1.0.0",
            "x-language":               "en-US",
            "r-timezone":               "Asia/Shanghai",
            "x-traffic-id":             claims.get("sub", ""),
            "connect-protocol-version": "1",
            "Content-Type":             "application/json",
            "Referer":                  "https://www.kimi.com/code/console",
            "User-Agent":               "Mozilla/5.0",
        }

    def _poll_sync(self) -> dict | None:
        if not self._token:
            log.error("Kimi: no auth token — set kimi_auth_token in config or save to %s", DEFAULT_TOKEN_FILE)
            return None

        try:
            headers = self._build_headers()
        except ValueError as e:
            log.error("Kimi: %s", e)
            return None

        cmd = ["curl", "-s", "-X", "POST", _USAGES_URL, "-d", _USAGES_BODY]
        for k, v in headers.items():
            cmd.extend(["-H", f"{k}: {v}"])

        try:
            result = subprocess.run(cmd, capture_output=True, text=True, timeout=15)
            data = json.loads(result.stdout)
        except Exception as e:
            log.error("Kimi API call failed: %s", e)
            return None

        if data.get("code") == "unauthenticated":
            log.error("Kimi auth failed — token may be expired. Renew kimi-auth token.")
            return None

        usages = data.get("usages", [])
        if not usages:
            log.warning("Kimi: empty usages response")
            return None

        now     = int(time.time())
        usage   = usages[0]
        detail  = usage.get("detail", {})
        limits  = usage.get("limits", [])

        # Weekly usage
        weekly_limit = int(detail.get("limit", 0)) or 1
        weekly_used  = int(detail.get("used",  0))
        weekly_pct   = round(weekly_used / weekly_limit * 100)
        weekly_reset = _iso_to_reset_mins(detail.get("resetTime", ""), now)

        # 5-hour rate-limit window (first entry in limits[])
        session_pct   = 0
        session_reset = -1
        if limits:
            lim        = limits[0].get("detail", {})
            lim_limit  = int(lim.get("limit", 100)) or 1
            lim_remain = int(lim.get("remaining", lim_limit))
            lim_used   = lim_limit - lim_remain
            session_pct   = round(lim_used / lim_limit * 100)
            session_reset = _iso_to_reset_mins(lim.get("resetTime", ""), now)

        payload = {
            "s":  session_pct,
            "sr": session_reset,
            "w":  weekly_pct,
            "wr": weekly_reset,
            "st": "allowed",
        }
        log.info("Kimi polled: session=%d%% weekly=%d%%", session_pct, weekly_pct)
        return payload

    async def poll(self) -> dict | None:
        return await asyncio.to_thread(self._poll_sync)
