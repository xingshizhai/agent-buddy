# daemon/services/claude.py
import json
import logging
import time
from pathlib import Path

import aiohttp

from .base import ServiceBase

log = logging.getLogger(__name__)

CREDENTIALS_FILE = Path.home() / ".claude" / ".credentials.json"

_API_URL = "https://api.anthropic.com/v1/messages"
_API_BODY = json.dumps({
    "model": "claude-haiku-4-5-20251001",
    "max_tokens": 1,
    "messages": [{"role": "user", "content": "hi"}],
})


class ClaudeService(ServiceBase):
    service_id = "claude"
    poll_interval = 60

    def _read_token(self) -> str:
        with open(CREDENTIALS_FILE) as f:
            creds = json.load(f)
        token = (creds.get("accessToken")
                 or creds.get("access_token")
                 or (creds.get("claudeAiOauth") or {}).get("accessToken")
                 or "")
        if not token:
            raise ValueError(f"No accessToken found in {CREDENTIALS_FILE}")
        return token

    async def poll(self) -> dict | None:
        try:
            token = self._read_token()
        except Exception as e:
            log.error("Cannot read Claude token: %s", e)
            return None

        now = int(time.time())
        headers = {
            "Authorization": f"Bearer {token}",
            "anthropic-version": "2023-06-01",
            "anthropic-beta": "oauth-2025-04-20",
            "Content-Type": "application/json",
            "User-Agent": "claude-code/2.1.5",
        }

        try:
            async with aiohttp.ClientSession() as session:
                async with session.post(
                    _API_URL,
                    headers=headers,
                    data=_API_BODY,
                    timeout=aiohttp.ClientTimeout(total=15),
                ) as resp:
                    resp_headers = resp.headers
        except Exception as e:
            log.error("Anthropic API call failed: %s", e)
            return None

        def hdr(name: str) -> str:
            return resp_headers.get(name, "")

        s5h = float(hdr("anthropic-ratelimit-unified-5h-utilization") or 0)
        s5r = hdr("anthropic-ratelimit-unified-5h-reset") or str(now)
        s7d = float(hdr("anthropic-ratelimit-unified-7d-utilization") or 0)
        s7r = hdr("anthropic-ratelimit-unified-7d-reset") or str(now)
        st  = hdr("anthropic-ratelimit-unified-5h-status") or "unknown"

        sp = round(s5h * 100)
        sr = max(0, round((int(s5r) - now) / 60)) if s5r.isdigit() else 0
        wp = round(s7d * 100)
        wr = max(0, round((int(s7r) - now) / 60)) if s7r.isdigit() else 0

        return {"s": sp, "sr": sr, "w": wp, "wr": wr, "st": st}
