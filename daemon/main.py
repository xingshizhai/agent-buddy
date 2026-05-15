"""Agent Buddy daemon — entry point.

Run from daemon/ directory:
    python main.py

Or from repo root:
    python -m daemon
"""
import sys
from pathlib import Path

# Support both `python main.py` (from daemon/) and `python -m daemon` (from repo root)
if __package__ is None:
    sys.path.insert(0, str(Path(__file__).parent.parent))

import asyncio
import logging

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

    backoff = 1
    try:
        while True:
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

            log.info("Reconnecting in %ds…", RECONNECT_WAIT)
            await asyncio.sleep(RECONNECT_WAIT)

    except (asyncio.CancelledError, KeyboardInterrupt):
        pass

    log.info("Daemon stopped")


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
