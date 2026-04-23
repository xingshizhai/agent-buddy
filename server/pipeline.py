"""
AI pipeline: ASR → LLM → TTS

One pipeline instance per WebSocket connection.
Keeps conversation history for multi-turn dialogue.
"""

import asyncio
import json
import logging
from typing import List, Dict

from fastapi import WebSocket

from providers.alibaba import StreamingASR, synthesize_to_stereo_pcm, stereo_to_mono
from providers.llm import make_llm

logger = logging.getLogger(__name__)

# Minimum sentence chunk length to trigger TTS (avoid tiny fragments)
TTS_CHUNK_MIN_CHARS = 10


class AIPipeline:
    def __init__(self, ws: WebSocket):
        self._ws      = ws
        self._llm     = make_llm()
        self._history: List[Dict] = []
        self._asr: StreamingASR | None = None
        self._loop    = asyncio.get_event_loop()

    # ── WebSocket helpers ─────────────────────────────────────────────────────

    async def _send_json(self, obj: dict):
        await self._ws.send_text(json.dumps(obj, ensure_ascii=False))

    async def _send_pcm(self, data: bytes):
        await self._ws.send_bytes(data)

    # ── Main loop ─────────────────────────────────────────────────────────────

    async def run(self):
        await self._send_json({"type": "ready"})
        logger.info("Pipeline ready, waiting for messages")

        async for message in self._ws.iter_text():
            try:
                msg = json.loads(message)
            except json.JSONDecodeError:
                continue
            await self._handle_control(msg)

    async def _handle_control(self, msg: dict):
        t = msg.get("type")
        if t == "start_speech":
            await self._on_start_speech()
        elif t == "end_speech":
            await self._on_end_speech()
        elif t == "audio_chunk":
            # binary audio arrives as raw WebSocket binary frames, not JSON
            pass
        elif t == "ping":
            await self._send_json({"type": "pong"})

    # ── Speech events ─────────────────────────────────────────────────────────

    async def _on_start_speech(self):
        logger.info("start_speech")
        self._asr = StreamingASR(self._loop)
        self._asr.start()
        await self._send_json({"type": "status", "msg": "Listening..."})

    async def _on_end_speech(self):
        logger.info("end_speech")
        if not self._asr:
            return
        try:
            await self._send_json({"type": "status", "msg": "Recognizing..."})
            transcript = await self._asr.finish()
            self._asr = None
        except Exception as e:
            logger.error(f"ASR error: {e}")
            await self._send_json({"type": "error", "msg": f"ASR failed: {e}"})
            self._asr = None
            return

        if not transcript.strip():
            await self._send_json({"type": "status", "msg": "Nothing heard, try again."})
            return

        await self._send_json({"type": "transcript", "text": transcript})
        await self._run_llm_tts(transcript)

    def feed_audio(self, pcm_stereo: bytes):
        """Called from WebSocket binary handler with stereo PCM from ESP32."""
        if self._asr:
            mono = stereo_to_mono(pcm_stereo)
            self._asr.feed(mono)

    # ── LLM + TTS ─────────────────────────────────────────────────────────────

    async def _run_llm_tts(self, user_text: str):
        await self._send_json({"type": "status", "msg": "Thinking..."})
        await self._send_json({"type": "response_start"})

        full_response = ""
        pending_text  = ""

        async for token in self._llm.stream_response(self._history, user_text):
            full_response += token
            pending_text  += token

            # Flush to TTS on sentence boundaries
            flush_text = _extract_sentences(pending_text)
            if flush_text:
                pending_text = pending_text[len(flush_text):]
                await self._tts_speak(flush_text)

        # Flush remainder
        if pending_text.strip():
            await self._tts_speak(pending_text)

        # Update conversation history
        self._history.append({"role": "user",      "content": user_text})
        self._history.append({"role": "assistant",  "content": full_response})
        # Keep last 10 turns to avoid unbounded growth
        if len(self._history) > 20:
            self._history = self._history[-20:]

        await self._send_json({"type": "response_end", "text": full_response})
        logger.info(f"Response done: {full_response!r}")

    async def _tts_speak(self, text: str):
        if not text.strip():
            return
        logger.info(f"TTS: {text!r}")
        try:
            chunks_sent = 0

            def on_chunk(pcm_stereo: bytes):
                nonlocal chunks_sent
                # Schedule send on event loop (callback is from TTS thread)
                asyncio.run_coroutine_threadsafe(
                    self._send_pcm(pcm_stereo), self._loop
                )
                chunks_sent += 1

            await synthesize_to_stereo_pcm(text, on_chunk)
            logger.debug(f"TTS sent {chunks_sent} chunks for: {text!r}")
        except Exception as e:
            logger.error(f"TTS error: {e}")

    async def cleanup(self):
        if self._asr:
            try:
                self._asr._rec and self._asr._rec.stop()
            except Exception:
                pass


# ── Helpers ───────────────────────────────────────────────────────────────────

_SENTENCE_END = set("。！？.!?\n")

def _extract_sentences(text: str) -> str:
    """Return the longest prefix of text ending on a sentence boundary."""
    last = -1
    for i, ch in enumerate(text):
        if ch in _SENTENCE_END:
            last = i
    if last >= 0 and last + 1 >= TTS_CHUNK_MIN_CHARS:
        return text[:last + 1]
    return ""
