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
