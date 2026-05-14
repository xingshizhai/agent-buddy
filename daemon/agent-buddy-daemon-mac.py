#!/usr/bin/env python3
"""
Agent Buddy Usage Daemon — macOS version (uses bleak / CoreBluetooth)
Polls Anthropic API usage, sends JSON to Agent Buddy ESP32 via BLE GATT.
"""

import asyncio
import json
import logging
import os
import signal
import subprocess
import sys
import time
from pathlib import Path

try:
    from bleak import BleakClient, BleakScanner
    from bleak.exc import BleakError
except ImportError:
    print("ERROR: bleak not installed. Run: pip3 install bleak")
    sys.exit(1)

# ── Configuration ──────────────────────────────────────────────────────────
DEVICE_NAME    = "Agent Buddy"
SERVICE_UUID   = "41474e54-4255-4459-0000-000000000001"
RX_CHAR_UUID   = "41474e54-4255-4459-0000-000000000002"  # daemon writes here
TX_CHAR_UUID   = "41474e54-4255-4459-0000-000000000003"  # ESP notifies ACK
REQ_CHAR_UUID  = "41474e54-4255-4459-0000-000000000004"  # ESP notifies refresh

POLL_INTERVAL  = 60   # seconds between API polls
SCAN_TIMEOUT   = 10   # seconds to scan for device
RECONNECT_WAIT = 5    # seconds before reconnect attempt

CREDENTIALS_FILE = Path.home() / ".claude" / ".credentials.json"
SAVED_MAC_FILE   = Path.home() / ".config" / "agent-buddy" / "ble-address"

logging.basicConfig(
    format="[%(asctime)s] %(message)s",
    datefmt="%H:%M:%S",
    level=logging.INFO,
)
log = logging.getLogger(__name__)

# ── Token ──────────────────────────────────────────────────────────────────
def read_token() -> str:
    with open(CREDENTIALS_FILE) as f:
        creds = json.load(f)
    token = creds.get("accessToken") or creds.get("access_token") or ""
    if not token:
        raise ValueError(f"No accessToken in {CREDENTIALS_FILE}")
    return token

# ── Anthropic API poll ─────────────────────────────────────────────────────
def poll_usage() -> str | None:
    try:
        token = read_token()
    except Exception as e:
        log.error("Cannot read token: %s", e)
        return None

    now = int(time.time())
    cmd = [
        "curl", "-s", "-D", "-", "-o", "/dev/null",
        "https://api.anthropic.com/v1/messages",
        "-H", f"Authorization: Bearer {token}",
        "-H", "anthropic-version: 2023-06-01",
        "-H", "anthropic-beta: oauth-2025-04-20",
        "-H", "Content-Type: application/json",
        "-H", "User-Agent: claude-code/2.1.5",
        "-d", '{"model":"claude-haiku-4-5-20251001","max_tokens":1,"messages":[{"role":"user","content":"hi"}]}',
    ]
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=15)
        headers = result.stdout
    except Exception as e:
        log.error("API call failed: %s", e)
        return None

    def header(name: str) -> str:
        for line in headers.splitlines():
            if line.lower().startswith(name.lower() + ":"):
                return line.split(":", 1)[1].strip()
        return ""

    s5h = float(header("anthropic-ratelimit-unified-5h-utilization") or 0)
    s5r = header("anthropic-ratelimit-unified-5h-reset") or str(now)
    s7d = float(header("anthropic-ratelimit-unified-7d-utilization") or 0)
    s7r = header("anthropic-ratelimit-unified-7d-reset") or str(now)
    st  = header("anthropic-ratelimit-unified-5h-status") or "unknown"

    sp  = round(s5h * 100)
    sr  = max(0, round((int(s5r) - now) / 60)) if s5r.isdigit() else 0
    wp  = round(s7d * 100)
    wr  = max(0, round((int(s7r) - now) / 60)) if s7r.isdigit() else 0

    payload = json.dumps({"s": sp, "sr": sr, "w": wp, "wr": wr, "st": st, "ok": True})
    log.info("Sending: %s", payload)
    return payload

# ── BLE device discovery ───────────────────────────────────────────────────
async def find_device():
    saved = None
    if SAVED_MAC_FILE.exists():
        saved = SAVED_MAC_FILE.read_text().strip()
        log.info("Trying cached address: %s", saved)

    log.info("Scanning for Agent Buddy device (%ds)…", SCAN_TIMEOUT)
    devices = await BleakScanner.discover(timeout=SCAN_TIMEOUT, return_adv=True)

    # Prefer saved address for fast reconnect
    if saved:
        for addr, (dev, adv) in devices.items():
            if addr.upper() == saved.upper():
                log.info("Found (cached): %s  [%s]", addr, dev.name or "?")
                return addr

    # Match by Service UUID (reliable even if macOS shows a cached name)
    for addr, (dev, adv) in devices.items():
        svc_uuids = [str(u).lower() for u in (adv.service_uuids or [])]
        if SERVICE_UUID.lower() in svc_uuids:
            log.info("Found by UUID: %s  [%s]", addr, dev.name or "?")
            SAVED_MAC_FILE.parent.mkdir(parents=True, exist_ok=True)
            SAVED_MAC_FILE.write_text(addr)
            return addr

    # Fallback: match by name
    for addr, (dev, adv) in devices.items():
        name = dev.name or adv.local_name or ""
        if DEVICE_NAME in name or "Claude" in name:
            log.info("Found by name: %s  [%s]", addr, name)
            SAVED_MAC_FILE.parent.mkdir(parents=True, exist_ok=True)
            SAVED_MAC_FILE.write_text(addr)
            return addr

    log.warning("Device not found (scanned %d devices)", len(devices))
    return None

# ── Main BLE session ────────────────────────────────────────────────────────
async def run_session(address: str):
    refresh_event = asyncio.Event()

    def on_notify(_, data: bytearray):
        log.info("Device requested refresh")
        refresh_event.set()

    log.info("Connecting to %s…", address)
    async with BleakClient(address, timeout=15) as client:
        log.info("Connected")

        # Find characteristics
        rx_char = tx_char = req_char = None
        for svc in client.services:
            for ch in svc.characteristics:
                uuid = str(ch.uuid).lower()
                if uuid == RX_CHAR_UUID:  rx_char  = ch
                if uuid == TX_CHAR_UUID:  tx_char  = ch
                if uuid == REQ_CHAR_UUID: req_char = ch

        if not rx_char:
            log.error("RX characteristic not found — wrong device?")
            return

        # Subscribe to refresh requests from ESP32
        if req_char and "notify" in req_char.properties:
            await client.start_notify(req_char, on_notify)
            log.info("Subscribed to refresh notifications")

        last_poll = 0

        while client.is_connected:
            now = time.time()
            if refresh_event.is_set() or (now - last_poll >= POLL_INTERVAL):
                refresh_event.clear()
                payload = poll_usage()
                if payload:
                    await client.write_gatt_char(rx_char, payload.encode(), response=False)
                    last_poll = time.time()
                else:
                    log.warning("Poll failed, will retry next interval")

            await asyncio.sleep(5)

        log.info("Device disconnected")

# ── Event loop ──────────────────────────────────────────────────────────────
async def main():
    log.info("=== Claude Usage Tracker Daemon (macOS/bleak) ===")
    log.info("Poll interval: %ds", POLL_INTERVAL)

    loop = asyncio.get_event_loop()
    stop = asyncio.Event()
    loop.add_signal_handler(signal.SIGINT,  stop.set)
    loop.add_signal_handler(signal.SIGTERM, stop.set)

    backoff = 1
    while not stop.is_set():
        try:
            address = await find_device()
            if not address:
                log.info("Retrying in %ds…", backoff)
                await asyncio.sleep(backoff)
                backoff = min(backoff * 2, 60)
                continue

            backoff = 1
            await run_session(address)

        except BleakError as e:
            log.error("BLE error: %s", e)
            # Invalidate cached address on connection failure
            if SAVED_MAC_FILE.exists():
                SAVED_MAC_FILE.unlink()
        except Exception as e:
            log.error("Unexpected error: %s", e)

        if not stop.is_set():
            log.info("Reconnecting in %ds…", RECONNECT_WAIT)
            await asyncio.sleep(RECONNECT_WAIT)

    log.info("Daemon stopped")

if __name__ == "__main__":
    asyncio.run(main())
