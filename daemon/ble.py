# daemon/ble.py
import asyncio
import logging
from collections.abc import AsyncIterator, Callable, Awaitable
from contextlib import asynccontextmanager
from pathlib import Path

from bleak import BleakClient, BleakScanner
from bleak.exc import BleakError

log = logging.getLogger(__name__)

SERVICE_UUID  = "41474e54-4255-4459-0000-000000000001"
RX_CHAR_UUID  = "41474e54-4255-4459-0000-000000000002"
TX_CHAR_UUID  = "41474e54-4255-4459-0000-000000000003"
REQ_CHAR_UUID = "41474e54-4255-4459-0000-000000000004"

SCAN_TIMEOUT = 10
DEVICE_NAME  = "Agent Buddy"

_ADDR_CACHE = Path.home() / ".config" / "agent-buddy" / "ble-address"

WriteFn = Callable[[bytes], Awaitable[None]]


def _load_cached_addr() -> str | None:
    try:
        addr = _ADDR_CACHE.read_text().strip()
        return addr if addr else None
    except OSError:
        return None


def _save_cached_addr(addr: str) -> None:
    try:
        _ADDR_CACHE.parent.mkdir(parents=True, exist_ok=True)
        _ADDR_CACHE.write_text(addr)
    except OSError:
        pass


def _clear_cached_addr() -> None:
    try:
        _ADDR_CACHE.unlink(missing_ok=True)
    except OSError:
        pass


async def find_device(scan_timeout: int = SCAN_TIMEOUT) -> str | None:
    """Return the BLE address of Agent Buddy.

    Tries the cached address first (works even when the device is already
    connected to bluetoothd — avoids the "not found in discovery" trap after
    an unclean daemon exit).  Falls back to a full scan on cache miss.
    """
    saved = _load_cached_addr()
    if saved:
        log.info("Trying cached address %s…", saved)
        device = await BleakScanner.find_device_by_address(saved, timeout=5)
        if device is not None:
            log.info("Found cached device: %s [%s]", saved, device.name or "?")
            return saved
        log.warning("Cached address %s not reachable, doing full scan…", saved)
        _clear_cached_addr()

    log.info("Scanning for %s (%ds)…", DEVICE_NAME, scan_timeout)
    devices = await BleakScanner.discover(timeout=scan_timeout, return_adv=True)

    for addr, (dev, adv) in devices.items():
        svc_uuids = [str(u).lower() for u in (adv.service_uuids or [])]
        if SERVICE_UUID.lower() in svc_uuids:
            log.info("Found by UUID: %s [%s]", addr, dev.name or "?")
            _save_cached_addr(addr)
            return addr

    for addr, (dev, adv) in devices.items():
        name = (dev.name or "") + (getattr(adv, "local_name", None) or "")
        if DEVICE_NAME in name:
            log.info("Found by name: %s [%s]", addr, dev.name)
            _save_cached_addr(addr)
            return addr

    log.warning("Device not found (%d scanned)", len(devices))
    return None


@asynccontextmanager
async def open_session(
    address: str,
    on_message: Callable[[dict], None],
) -> AsyncIterator[tuple[WriteFn, Awaitable[None]]]:
    """Connect to the device and yield ``(write_fn, until_disconnect)``.

    Clears the address cache on connection failure so the next call to
    ``find_device()`` does a fresh scan.
    """
    import daemon.protocol as proto

    connected = True

    def _on_disconnect(_: BleakClient) -> None:
        nonlocal connected
        connected = False
        log.info("BLE device disconnected")

    try:
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

    except Exception:
        # Connection failed — clear cache so next run does a fresh scan
        _clear_cached_addr()
        raise
