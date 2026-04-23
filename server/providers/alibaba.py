"""
Alibaba Bailian (DashScope) ASR + TTS provider.

ASR: paraformer-realtime-v2  (streaming recognition)
TTS: cosyvoice-v2            (streaming synthesis → PCM)
"""

import asyncio
import struct
import logging
import threading
from typing import Callable, Optional

import dashscope
from dashscope.audio.asr import Recognition, RecognitionCallback, RecognitionResult
from dashscope.audio.tts_v2 import SpeechSynthesizer, AudioFormat, ResultCallback

from config import DASHSCOPE_API_KEY, TTS_VOICE, ASR_SAMPLE_RATE

logger = logging.getLogger(__name__)

dashscope.api_key = DASHSCOPE_API_KEY


# ── ASR ───────────────────────────────────────────────────────────────────────

class _ASRCallback(RecognitionCallback):
    def __init__(self, loop: asyncio.AbstractEventLoop,
                 result_queue: asyncio.Queue):
        self._loop   = loop
        self._queue  = result_queue
        self._final  = ""

    def on_event(self, result: RecognitionResult):
        if result.output and result.output.get("sentence"):
            sentence = result.output["sentence"]
            text = sentence.get("text", "")
            if sentence.get("end_time") is not None:
                # final sentence
                self._final += text
            logger.debug(f"ASR partial: {text}")

    def on_complete(self):
        asyncio.run_coroutine_threadsafe(
            self._queue.put(("done", self._final)), self._loop
        )

    def on_error(self, result):
        logger.error(f"ASR error: {result}")
        asyncio.run_coroutine_threadsafe(
            self._queue.put(("error", str(result))), self._loop
        )


class StreamingASR:
    """
    Thin async wrapper around DashScope Recognition.

    Usage:
        asr = StreamingASR(loop)
        asr.start()
        asr.feed(pcm_mono_bytes)   # call from any thread
        transcript = await asr.finish()
    """

    def __init__(self, loop: asyncio.AbstractEventLoop):
        self._loop     = loop
        self._queue: asyncio.Queue = asyncio.Queue()
        self._callback = _ASRCallback(loop, self._queue)
        self._rec: Optional[Recognition] = None

    def start(self):
        self._rec = Recognition(
            model="paraformer-realtime-v2",
            format="pcm",
            sample_rate=ASR_SAMPLE_RATE,
            language_hints=["zh", "en"],
            callback=self._callback,
        )
        self._rec.start()
        logger.info("ASR started")

    def feed(self, pcm_mono: bytes):
        if self._rec:
            self._rec.send_audio_frame(pcm_mono)

    async def finish(self) -> str:
        if self._rec:
            self._rec.stop()
            self._rec = None
        event, value = await asyncio.wait_for(self._queue.get(), timeout=10.0)
        if event == "error":
            raise RuntimeError(f"ASR failed: {value}")
        logger.info(f"ASR transcript: {value!r}")
        return value


# ── TTS ───────────────────────────────────────────────────────────────────────

class _TTSCallback(ResultCallback):
    def __init__(self, loop: asyncio.AbstractEventLoop,
                 audio_queue: asyncio.Queue):
        self._loop  = loop
        self._queue = audio_queue

    def on_event(self, message):
        pass

    def on_data(self, data: bytes):
        asyncio.run_coroutine_threadsafe(
            self._queue.put(data), self._loop
        )

    def on_complete(self):
        asyncio.run_coroutine_threadsafe(
            self._queue.put(None), self._loop  # sentinel
        )

    def on_error(self, message):
        logger.error(f"TTS error: {message}")
        asyncio.run_coroutine_threadsafe(
            self._queue.put(None), self._loop
        )


async def synthesize_to_stereo_pcm(
    text: str,
    on_chunk: Callable[[bytes], None],
):
    """
    Synthesize `text` with CosyVoice-v2.
    Calls on_chunk with stereo 16kHz 16-bit PCM chunks as they arrive.
    """
    loop  = asyncio.get_event_loop()
    queue: asyncio.Queue = asyncio.Queue()
    cb    = _TTSCallback(loop, queue)

    synth = SpeechSynthesizer(
        model="cosyvoice-v2",
        voice=TTS_VOICE,
        format=AudioFormat.PCM_16000HZ_MONO_16BIT,
        callback=cb,
    )

    def _run():
        synth.streaming_call(text)
        synth.streaming_complete()

    threading.Thread(target=_run, daemon=True).start()

    while True:
        chunk = await asyncio.wait_for(queue.get(), timeout=15.0)
        if chunk is None:
            break
        stereo = mono_to_stereo(chunk)
        on_chunk(stereo)


def mono_to_stereo(pcm_mono: bytes) -> bytes:
    """Duplicate mono channel to stereo (interleaved L/R)."""
    n_samples = len(pcm_mono) // 2
    out = bytearray(n_samples * 4)
    for i in range(n_samples):
        sample = pcm_mono[i*2 : i*2+2]
        out[i*4 : i*4+2] = sample   # L
        out[i*4+2 : i*4+4] = sample  # R
    return bytes(out)


def stereo_to_mono(pcm_stereo: bytes) -> bytes:
    """Extract left channel from stereo interleaved PCM."""
    n_samples = len(pcm_stereo) // 4  # 4 bytes per stereo frame
    out = bytearray(n_samples * 2)
    for i in range(n_samples):
        out[i*2 : i*2+2] = pcm_stereo[i*4 : i*4+2]  # take L channel
    return bytes(out)
