"""
LLM provider abstraction. Add new providers by subclassing BaseLLM.
"""

from abc import ABC, abstractmethod
from typing import AsyncIterator, List, Dict
import logging

from openai import AsyncOpenAI

from config import DEEPSEEK_API_KEY, SYSTEM_PROMPT, LLM_PROVIDER

logger = logging.getLogger(__name__)


class BaseLLM(ABC):
    @abstractmethod
    async def stream_response(
        self,
        history: List[Dict],
        user_text: str,
    ) -> AsyncIterator[str]:
        """Yield text tokens as they arrive."""
        ...


class DeepSeekLLM(BaseLLM):
    def __init__(self):
        self._client = AsyncOpenAI(
            api_key=DEEPSEEK_API_KEY,
            base_url="https://api.deepseek.com/v1",
        )

    async def stream_response(
        self,
        history: List[Dict],
        user_text: str,
    ) -> AsyncIterator[str]:
        messages = [{"role": "system", "content": SYSTEM_PROMPT}]
        messages.extend(history)
        messages.append({"role": "user", "content": user_text})

        logger.info(f"DeepSeek request: {user_text!r}")
        stream = await self._client.chat.completions.create(
            model="deepseek-chat",
            messages=messages,
            stream=True,
            temperature=0.7,
            max_tokens=512,
        )
        async for chunk in stream:
            delta = chunk.choices[0].delta.content
            if delta:
                yield delta


# ── Factory ───────────────────────────────────────────────────────────────────

def make_llm(provider: str = LLM_PROVIDER) -> BaseLLM:
    if provider == "deepseek":
        return DeepSeekLLM()
    raise ValueError(f"Unknown LLM provider: {provider!r}")
