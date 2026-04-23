from dotenv import load_dotenv
import os

load_dotenv()

DASHSCOPE_API_KEY = os.environ["DASHSCOPE_API_KEY"]
DEEPSEEK_API_KEY  = os.environ["DEEPSEEK_API_KEY"]

SERVER_HOST = os.getenv("SERVER_HOST", "0.0.0.0")
SERVER_PORT = int(os.getenv("SERVER_PORT", "8765"))

TTS_VOICE    = os.getenv("TTS_VOICE", "longxiaochun_v2")
LLM_PROVIDER = os.getenv("LLM_PROVIDER", "deepseek")

SYSTEM_PROMPT = os.getenv(
    "SYSTEM_PROMPT",
    "你是一个智能助手，名叫小明。请用简短、自然的口语化中文回答。"
)

# Audio: ESP32 sends stereo 16kHz PCM; ASR expects mono 16kHz PCM
ASR_SAMPLE_RATE  = 16000
TTS_SAMPLE_RATE  = 16000
ESP32_CHANNELS   = 2   # stereo
ASR_CHANNELS     = 1   # mono
