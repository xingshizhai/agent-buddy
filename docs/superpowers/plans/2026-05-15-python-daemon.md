# Python Daemon Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace `daemon/agent-buddy-daemon.sh` and `daemon/agent-buddy-daemon-mac.py` with a cross-platform modular Python package that speaks the new JSON-envelope BLE protocol.

**Architecture:** `daemon/` is a Python package run with `python -m daemon` from the repo root. `protocol.py` handles message framing, `ble.py` manages the BLE connection using bleak, each service is a `ServiceBase` subclass with its own `poll()` coroutine, and `main.py` orchestrates independent `asyncio.Task`s — one per service plus a BLE watchdog.

**Tech Stack:** Python 3.11+, bleak ≥ 0.21, aiohttp ≥ 3.9, pytest ≥ 8, pytest-asyncio ≥ 0.23

**Spec:** `docs/superpowers/specs/2026-05-15-multi-service-protocol-ui-design.md`

---

## File Map

| Action | Path | Responsibility |
|--------|------|----------------|
| Create | `daemon/__init__.py` | Makes `daemon` a Python package |
| Create | `daemon/protocol.py` | `make_data()`, `parse()` — pure serialisation, no I/O |
| Create | `daemon/ble.py` | BLE scan, connect, notify subscribe, write |
| Create | `daemon/services/__init__.py` | Package init |
| Create | `daemon/services/base.py` | `ServiceBase` abstract class |
| Create | `daemon/services/claude.py` | Claude OAuth + Anthropic API poller |
| Create | `daemon/main.py` | Lifecycle, reconnect loop, asyncio orchestration |
| Create | `daemon/requirements.txt` | Runtime deps |
| Create | `daemon/requirements-dev.txt` | Test deps |
| Create | `daemon/tests/__init__.py` | Test package init |
| Create | `daemon/tests/test_protocol.py` | Unit tests for `protocol.py` |
| Create | `daemon/tests/test_claude.py` | Unit tests for `ClaudeService.poll()` |
| Modify | `daemon/agent-buddy-daemon.service` | Update `ExecStart` to new entry point |
| Delete | `daemon/agent-buddy-daemon.sh` | Superseded |
| Delete | `daemon/agent-buddy-daemon-mac.py` | Superseded |

---

## Task 1: Package Scaffold and Dependencies

**Files:**
- Create: `daemon/__init__.py`
- Create: `daemon/services/__init__.py`
- Create: `daemon/tests/__init__.py`
- Create: `daemon/requirements.txt`
- Create: `daemon/requirements-dev.txt`

- [ ] **Step 1: Create package init files**

```python
# daemon/__init__.py  (empty)
```

```python
# daemon/services/__init__.py  (empty)
```

```python
# daemon/tests/__init__.py  (empty)
```

- [ ] **Step 2: Create `daemon/requirements.txt`**

```
bleak>=0.21
aiohttp>=3.9
```

- [ ] **Step 3: Create `daemon/requirements-dev.txt`**

```
-r requirements.txt
pytest>=8.0
pytest-asyncio>=0.23
```

- [ ] **Step 4: Install dev dependencies**

Run from repo root:
```bash
pip install -r daemon/requirements-dev.txt
```

Expected: packages install without error.

- [ ] **Step 5: Verify pytest can discover the test directory**

```bash
cd /data/Work/esp32/agent-buddy
python -m pytest daemon/tests/ --collect-only
```

Expected output: `no tests ran` (no tests yet — that's correct).

- [ ] **Step 6: Commit**

```bash
git add daemon/__init__.py daemon/services/__init__.py daemon/tests/__init__.py \
        daemon/requirements.txt daemon/requirements-dev.txt
git commit -m "chore: scaffold Python daemon package structure"
```

---

## Task 2: Protocol Module (TDD)

**Files:**
- Create: `daemon/protocol.py`
- Create: `daemon/tests/test_protocol.py`

- [ ] **Step 1: Write failing tests**

```python
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
```

- [ ] **Step 2: Run tests to confirm they fail**

```bash
python -m pytest daemon/tests/test_protocol.py -v
```

Expected: `ERROR` — `ModuleNotFoundError: No module named 'daemon.protocol'`.

- [ ] **Step 3: Implement `daemon/protocol.py`**

```python
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
```

- [ ] **Step 4: Run tests and confirm they pass**

```bash
python -m pytest daemon/tests/test_protocol.py -v
```

Expected: all 10 tests `PASSED`.

- [ ] **Step 5: Commit**

```bash
git add daemon/protocol.py daemon/tests/test_protocol.py
git commit -m "feat: add protocol serialisation module with tests"
```

---

## Task 3: ServiceBase Abstract Class

**Files:**
- Create: `daemon/services/base.py`

No unit tests — abstract interface, tested via concrete implementations.

- [ ] **Step 1: Create `daemon/services/base.py`**

```python
# daemon/services/base.py
from abc import ABC, abstractmethod


class ServiceBase(ABC):
    """Base class for all service pollers.

    Subclasses set ``service_id`` and ``poll_interval`` as class attributes
    and implement ``poll()``.
    """

    service_id: str    # e.g. "claude", "cursor"
    poll_interval: int  # seconds between polls

    @abstractmethod
    async def poll(self) -> dict | None:
        """Poll the upstream service.

        Returns a payload dict suitable for ``protocol.make_data()``,
        or ``None`` if the poll failed (daemon will retry next interval).
        """
```

- [ ] **Step 2: Verify import**

```bash
python -c "from daemon.services.base import ServiceBase; print('ok')"
```

Expected: `ok`.

- [ ] **Step 3: Commit**

```bash
git add daemon/services/base.py
git commit -m "feat: add ServiceBase abstract class"
```

---

## Task 4: Claude Service (TDD)

**Files:**
- Create: `daemon/services/claude.py`
- Create: `daemon/tests/test_claude.py`

- [ ] **Step 1: Write failing tests**

```python
# daemon/tests/test_claude.py
import json
import pytest
from unittest.mock import AsyncMock, MagicMock, patch
from daemon.services.claude import ClaudeService


@pytest.fixture
def svc():
    return ClaudeService()


def test_service_id(svc):
    assert svc.service_id == "claude"


def test_poll_interval(svc):
    assert svc.poll_interval == 60


@pytest.mark.asyncio
async def test_poll_returns_none_on_missing_credentials(svc, tmp_path, monkeypatch):
    monkeypatch.setattr("daemon.services.claude.CREDENTIALS_FILE", tmp_path / "missing.json")
    result = await svc.poll()
    assert result is None


@pytest.mark.asyncio
async def test_poll_returns_none_on_empty_token(svc, tmp_path, monkeypatch):
    creds = tmp_path / "creds.json"
    creds.write_text(json.dumps({"accessToken": ""}))
    monkeypatch.setattr("daemon.services.claude.CREDENTIALS_FILE", creds)
    result = await svc.poll()
    assert result is None


@pytest.mark.asyncio
async def test_poll_builds_payload(svc, tmp_path, monkeypatch):
    creds = tmp_path / "creds.json"
    creds.write_text(json.dumps({"accessToken": "tok-test"}))
    monkeypatch.setattr("daemon.services.claude.CREDENTIALS_FILE", creds)

    fixed_now = 1_000_000
    mock_headers = {
        "anthropic-ratelimit-unified-5h-utilization": "0.42",
        "anthropic-ratelimit-unified-5h-reset": str(fixed_now + 10_800),   # +3h
        "anthropic-ratelimit-unified-7d-utilization": "0.17",
        "anthropic-ratelimit-unified-7d-reset": str(fixed_now + 374_400),  # +6d14h
        "anthropic-ratelimit-unified-5h-status": "allowed",
    }

    # Build mock aiohttp response
    mock_resp = AsyncMock()
    mock_resp.headers = mock_headers
    mock_resp.__aenter__ = AsyncMock(return_value=mock_resp)
    mock_resp.__aexit__ = AsyncMock(return_value=False)

    mock_session = AsyncMock()
    mock_session.post = MagicMock(return_value=mock_resp)
    mock_session.__aenter__ = AsyncMock(return_value=mock_session)
    mock_session.__aexit__ = AsyncMock(return_value=False)

    with patch("daemon.services.claude.aiohttp.ClientSession", return_value=mock_session):
        with patch("daemon.services.claude.time.time", return_value=fixed_now):
            result = await svc.poll()

    assert result is not None
    assert result["s"] == 42        # round(0.42 * 100)
    assert result["sr"] == 180      # 10800 / 60
    assert result["w"] == 17        # round(0.17 * 100)
    assert result["wr"] == 6240     # 374400 / 60
    assert result["st"] == "allowed"


@pytest.mark.asyncio
async def test_poll_returns_none_on_api_error(svc, tmp_path, monkeypatch):
    creds = tmp_path / "creds.json"
    creds.write_text(json.dumps({"accessToken": "tok-test"}))
    monkeypatch.setattr("daemon.services.claude.CREDENTIALS_FILE", creds)

    mock_session = AsyncMock()
    mock_session.post = MagicMock(side_effect=Exception("network error"))
    mock_session.__aenter__ = AsyncMock(return_value=mock_session)
    mock_session.__aexit__ = AsyncMock(return_value=False)

    with patch("daemon.services.claude.aiohttp.ClientSession", return_value=mock_session):
        result = await svc.poll()

    assert result is None
```

- [ ] **Step 2: Run tests to confirm they fail**

```bash
python -m pytest daemon/tests/test_claude.py -v
```

Expected: `ERROR` — `ModuleNotFoundError: No module named 'daemon.services.claude'`.

- [ ] **Step 3: Implement `daemon/services/claude.py`**

```python
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
        token = creds.get("accessToken") or creds.get("access_token") or ""
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
```

- [ ] **Step 4: Run tests and confirm they pass**

```bash
python -m pytest daemon/tests/test_claude.py -v
```

Expected: all 6 tests `PASSED`.

- [ ] **Step 5: Commit**

```bash
git add daemon/services/claude.py daemon/tests/test_claude.py
git commit -m "feat: add Claude service with OAuth token poller"
```

---

## Task 5: BLE Module

**Files:**
- Create: `daemon/ble.py`

Unit tests are not practical without hardware. Verify via import check and manual run.

- [ ] **Step 1: Create `daemon/ble.py`**

```python
# daemon/ble.py
import asyncio
import logging
from collections.abc import AsyncIterator, Callable, Awaitable
from contextlib import asynccontextmanager

from bleak import BleakClient, BleakScanner
from bleak.exc import BleakError

log = logging.getLogger(__name__)

SERVICE_UUID  = "41474e54-4255-4459-0000-000000000001"
RX_CHAR_UUID  = "41474e54-4255-4459-0000-000000000002"
TX_CHAR_UUID  = "41474e54-4255-4459-0000-000000000003"
REQ_CHAR_UUID = "41474e54-4255-4459-0000-000000000004"

SCAN_TIMEOUT = 10
DEVICE_NAME  = "Agent Buddy"

WriteFn = Callable[[bytes], Awaitable[None]]


async def find_device() -> str | None:
    """Scan for Agent Buddy and return its BLE address, or None if not found."""
    log.info("Scanning for %s (%ds)…", DEVICE_NAME, SCAN_TIMEOUT)
    devices = await BleakScanner.discover(timeout=SCAN_TIMEOUT, return_adv=True)

    for addr, (dev, adv) in devices.items():
        svc_uuids = [str(u).lower() for u in (adv.service_uuids or [])]
        if SERVICE_UUID.lower() in svc_uuids:
            log.info("Found by UUID: %s [%s]", addr, dev.name or "?")
            return addr

    for addr, (dev, adv) in devices.items():
        name = (dev.name or "") + (getattr(adv, "local_name", None) or "")
        if DEVICE_NAME in name:
            log.info("Found by name: %s [%s]", addr, dev.name)
            return addr

    log.warning("Device not found (%d scanned)", len(devices))
    return None


@asynccontextmanager
async def open_session(
    address: str,
    on_message: Callable[[dict], None],
) -> AsyncIterator[tuple[WriteFn, Awaitable[None]]]:
    """Connect to the device and yield ``(write_fn, until_disconnect)``.

    ``write_fn(data)``      — sends bytes to the RX characteristic.
    ``until_disconnect``    — coroutine that returns when the device disconnects.

    Usage::

        async with ble.open_session(addr, cb) as (write, disc):
            tasks = [asyncio.create_task(some_loop(write))]
            await asyncio.wait([asyncio.create_task(disc)] + tasks,
                               return_when=asyncio.FIRST_COMPLETED)
    """
    import daemon.protocol as proto

    connected = True

    def _on_disconnect(_: BleakClient) -> None:
        nonlocal connected
        connected = False
        log.info("BLE device disconnected")

    async with BleakClient(
        address, timeout=15, disconnected_callback=_on_disconnect
    ) as client:
        log.info("Connected to %s", address)

        rx_char = tx_char = req_char = None
        for svc in client.services:
            for ch in svc.characteristics:
                u = str(ch.uuid).lower()
                if u == RX_CHAR_UUID:  rx_char  = ch
                if u == TX_CHAR_UUID:  tx_char  = ch
                if u == REQ_CHAR_UUID: req_char = ch

        if rx_char is None:
            raise RuntimeError("RX characteristic not found — is this the right device?")

        def _on_notify(_, data: bytearray) -> None:
            try:
                msg = proto.parse(bytes(data))
                on_message(msg)
            except Exception as e:
                log.error("Notify parse error: %s", e)

        if tx_char and "notify" in tx_char.properties:
            await client.start_notify(tx_char, _on_notify)
            log.info("Subscribed to TX notifications")
        if req_char and "notify" in req_char.properties:
            await client.start_notify(req_char, _on_notify)
            log.info("Subscribed to REQ notifications")

        async def write(data: bytes) -> None:
            if not connected:
                raise BleakError("Device is disconnected")
            await client.write_gatt_char(rx_char, data, response=False)

        async def until_disconnect() -> None:
            while connected:
                await asyncio.sleep(0.5)

        yield write, until_disconnect()
```

- [ ] **Step 2: Verify import**

```bash
python -c "from daemon.ble import find_device, open_session; print('ok')"
```

Expected: `ok`.

- [ ] **Step 3: Commit**

```bash
git add daemon/ble.py
git commit -m "feat: add BLE scan/connect module using bleak"
```

---

## Task 6: Main Entry Point

**Files:**
- Create: `daemon/main.py`

- [ ] **Step 1: Create `daemon/main.py`**

```python
# daemon/main.py
"""Agent Buddy daemon — entry point.

Run from repo root:
    python -m daemon
"""
import asyncio
import logging
import signal

from daemon import ble, protocol
from daemon.services.claude import ClaudeService

log = logging.getLogger(__name__)

# ── Service registry ──────────────────────────────────────────────────────────
# Add new services here; they start automatically on next connection.
SERVICES = [
    ClaudeService(),
]

RECONNECT_WAIT = 5
MAX_BACKOFF    = 60


async def _poll_loop(
    svc,
    wake_event: asyncio.Event,
    write_fn,
) -> None:
    """Poll one service on its interval; wake early when wake_event is set."""
    while True:
        payload = await svc.poll()
        if payload is not None:
            data = protocol.make_data(svc.service_id, payload)
            try:
                await write_fn(data)
                log.info("Sent %s data (%d bytes)", svc.service_id, len(data))
            except Exception as e:
                log.error("BLE write failed for %s: %s", svc.service_id, e)
                return  # let run_session handle reconnect

        # Wait for poll_interval or an early-wake from a req message
        try:
            await asyncio.wait_for(
                asyncio.shield(wake_event.wait()),
                timeout=svc.poll_interval,
            )
            wake_event.clear()
            log.info("Early wake-up for %s (req from device)", svc.service_id)
        except asyncio.TimeoutError:
            pass


async def run_session(address: str) -> None:
    req_events: dict[str, asyncio.Event] = {
        svc.service_id: asyncio.Event() for svc in SERVICES
    }
    cap_received = asyncio.Event()

    def on_message(msg: dict) -> None:
        t = msg.get("type")
        if t == "cap":
            log.info("Device cap: svcs=%s screens=%s", msg.get("svcs"), msg.get("screens"))
            cap_received.set()
        elif t == "req":
            svc_id = msg.get("svc")
            if svc_id:
                if svc_id in req_events:
                    req_events[svc_id].set()
                else:
                    log.warning("req for unknown svc: %s", svc_id)
            else:
                for ev in req_events.values():
                    ev.set()
        elif t == "ack":
            log.debug("ACK ok=%s", msg.get("ok"))
        elif t == "err":
            log.warning("Device error %s: %s", msg.get("code"), msg.get("msg"))
        else:
            log.debug("Unknown message type: %s", t)

    async with ble.open_session(address, on_message) as (write_fn, until_disconnect):
        # Allow up to 5 s for the cap message; proceed even if it doesn't arrive.
        try:
            await asyncio.wait_for(cap_received.wait(), timeout=5.0)
        except asyncio.TimeoutError:
            log.warning("No cap message received within 5 s — proceeding anyway")

        poll_tasks = [
            asyncio.create_task(
                _poll_loop(svc, req_events[svc.service_id], write_fn),
                name=f"poll-{svc.service_id}",
            )
            for svc in SERVICES
        ]
        watchdog = asyncio.create_task(until_disconnect, name="ble-watchdog")

        _done, pending = await asyncio.wait(
            [watchdog, *poll_tasks],
            return_when=asyncio.FIRST_COMPLETED,
        )

        for t in pending:
            t.cancel()
        await asyncio.gather(*pending, return_exceptions=True)
        log.info("Session ended")


async def main() -> None:
    logging.basicConfig(
        format="[%(asctime)s] %(levelname)s %(name)s: %(message)s",
        datefmt="%H:%M:%S",
        level=logging.INFO,
    )
    log.info("=== Agent Buddy Daemon ===")
    log.info("Services: %s", [s.service_id for s in SERVICES])

    loop = asyncio.get_event_loop()
    stop = asyncio.Event()
    loop.add_signal_handler(signal.SIGINT,  stop.set)
    loop.add_signal_handler(signal.SIGTERM, stop.set)

    backoff = 1
    while not stop.is_set():
        address = await ble.find_device()
        if address is None:
            log.info("Device not found, retrying in %ds…", backoff)
            await asyncio.sleep(backoff)
            backoff = min(backoff * 2, MAX_BACKOFF)
            continue

        backoff = 1
        try:
            await run_session(address)
        except Exception as e:
            log.error("Session error: %s", e)

        if not stop.is_set():
            log.info("Reconnecting in %ds…", RECONNECT_WAIT)
            await asyncio.sleep(RECONNECT_WAIT)

    log.info("Daemon stopped")


if __name__ == "__main__":
    asyncio.run(main())
```

- [ ] **Step 2: Add `daemon/__main__.py` so `python -m daemon` works**

```python
# daemon/__main__.py
from daemon.main import main
import asyncio
asyncio.run(main())
```

- [ ] **Step 3: Verify the module can be imported without errors**

```bash
python -c "import daemon.main; print('ok')"
```

Expected: `ok` (no hardware needed for import check).

- [ ] **Step 4: Commit**

```bash
git add daemon/main.py daemon/__main__.py
git commit -m "feat: add daemon main orchestration loop"
```

---

## Task 7: Systemd Service Update and Cleanup

**Files:**
- Modify: `daemon/agent-buddy-daemon.service`
- Delete: `daemon/agent-buddy-daemon.sh`
- Delete: `daemon/agent-buddy-daemon-mac.py`

- [ ] **Step 1: Update the systemd unit file**

Replace the full contents of `daemon/agent-buddy-daemon.service` with:

```ini
[Unit]
Description=Agent Buddy BLE Usage Daemon
Requires=bluetooth.target
After=bluetooth.target

[Service]
Type=simple
WorkingDirectory=%h/Data/Work/esp32/agent-buddy
ExecStart=/usr/bin/python3 -m daemon
Restart=on-failure
RestartSec=5
Environment=PYTHONUNBUFFERED=1

[Install]
WantedBy=default.target
```

> **Note:** Adjust `WorkingDirectory` to the actual repo path on the target machine before installing.

- [ ] **Step 2: Remove superseded files**

```bash
git rm daemon/agent-buddy-daemon.sh daemon/agent-buddy-daemon-mac.py
```

- [ ] **Step 3: Run all daemon tests one final time**

```bash
python -m pytest daemon/tests/ -v
```

Expected: all tests `PASSED`.

- [ ] **Step 4: Commit**

```bash
git add daemon/agent-buddy-daemon.service
git commit -m "feat: complete Python daemon rewrite; remove old bash/mac scripts"
```

---

## Manual Smoke Test (requires hardware)

After all tasks pass:

```bash
# From repo root, with ESP32 powered and advertising
python -m daemon
```

Expected log lines (in order):
```
[HH:MM:SS] INFO daemon.ble: Scanning for Agent Buddy (10s)…
[HH:MM:SS] INFO daemon.ble: Found by UUID: XX:XX:XX:XX:XX:XX [Agent Buddy]
[HH:MM:SS] INFO daemon.ble: Connected to XX:XX:XX:XX:XX:XX
[HH:MM:SS] INFO daemon.ble: Subscribed to TX notifications
[HH:MM:SS] INFO daemon.ble: Subscribed to REQ notifications
[HH:MM:SS] INFO daemon.main: Device cap: svcs=['claude'] screens=['usage','ble']
[HH:MM:SS] INFO daemon.main: Sent claude data (XX bytes)
```
