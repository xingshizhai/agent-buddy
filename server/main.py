"""
Agent Buddy WebSocket server.

Handles one ESP32 client per connection.
Text frames: JSON control messages
Binary frames: raw PCM 16-bit stereo 16kHz
"""

import asyncio
import json
import logging

import uvicorn
from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from starlette.websockets import WebSocketState

from config import SERVER_HOST, SERVER_PORT
from pipeline import AIPipeline

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s %(levelname)s %(name)s: %(message)s",
)
logger = logging.getLogger(__name__)

app = FastAPI(title="Agent Buddy Server")


@app.websocket("/ws/chat")
async def chat_endpoint(websocket: WebSocket):
    await websocket.accept()
    client = websocket.client
    logger.info(f"Client connected: {client}")

    pipeline = AIPipeline(websocket)

    async def receive_loop():
        """Receive both text and binary frames."""
        while True:
            message = await websocket.receive()
            if message["type"] == "websocket.disconnect":
                break
            elif message["type"] == "websocket.receive":
                if "text" in message and message["text"]:
                    try:
                        msg = json.loads(message["text"])
                        await pipeline._handle_control(msg)
                    except json.JSONDecodeError:
                        pass
                elif "bytes" in message and message["bytes"]:
                    pipeline.feed_audio(message["bytes"])

    try:
        await receive_loop()
    except WebSocketDisconnect:
        pass
    except Exception as e:
        logger.error(f"Connection error: {e}", exc_info=True)
    finally:
        await pipeline.cleanup()
        logger.info(f"Client disconnected: {client}")


@app.get("/health")
async def health():
    return {"status": "ok"}


if __name__ == "__main__":
    uvicorn.run(
        "main:app",
        host=SERVER_HOST,
        port=SERVER_PORT,
        ws_ping_interval=20,
        ws_ping_timeout=30,
        log_level="info",
    )
