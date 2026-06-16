# daemon/tests/test_claude.py
import json
import pytest
from unittest.mock import MagicMock, patch
from daemon.services.claude import ClaudeService


@pytest.fixture
def svc():
    return ClaudeService()   # all defaults


def test_service_id(svc):
    assert svc.service_id == "claude"


def test_poll_interval_default(svc):
    assert svc.poll_interval == 60


def test_poll_interval_custom():
    svc = ClaudeService(poll_interval=120)
    assert svc.poll_interval == 120


def test_proxy_stored():
    svc = ClaudeService(proxy_url="http://127.0.0.1:7890")
    assert svc._proxy_url == "http://127.0.0.1:7890"


@pytest.mark.asyncio
async def test_poll_returns_none_on_missing_credentials(tmp_path):
    svc = ClaudeService(credentials_file=str(tmp_path / "missing.json"))
    result = await svc.poll()
    assert result is None


@pytest.mark.asyncio
async def test_poll_returns_none_on_empty_token(tmp_path):
    creds = tmp_path / "creds.json"
    creds.write_text(json.dumps({"accessToken": ""}))
    svc = ClaudeService(credentials_file=str(creds))
    result = await svc.poll()
    assert result is None


def test_read_token_from_nested_claudeAiOauth(tmp_path):
    creds = tmp_path / "creds.json"
    creds.write_text(json.dumps({"claudeAiOauth": {"accessToken": "nested-token"}}))
    svc = ClaudeService(credentials_file=str(creds))
    assert svc._read_token() == "nested-token"


@pytest.mark.asyncio
async def test_poll_builds_payload(tmp_path):
    creds = tmp_path / "creds.json"
    creds.write_text(json.dumps({"accessToken": "tok-test"}))
    svc = ClaudeService(credentials_file=str(creds))

    fixed_now = 1_000_000
    fake_curl_output = (
        "HTTP/2 200 \r\n"
        "anthropic-ratelimit-unified-5h-utilization: 0.42\r\n"
        f"anthropic-ratelimit-unified-5h-reset: {fixed_now + 10_800}\r\n"
        "anthropic-ratelimit-unified-7d-utilization: 0.17\r\n"
        f"anthropic-ratelimit-unified-7d-reset: {fixed_now + 374_400}\r\n"
        "anthropic-ratelimit-unified-5h-status: allowed\r\n"
        "\r\n"
    )
    mock_result = MagicMock()
    mock_result.stdout = fake_curl_output

    with patch("daemon.services.claude.subprocess.run", return_value=mock_result):
        with patch("daemon.services.claude.time.time", return_value=fixed_now):
            result = await svc.poll()

    assert result is not None
    assert result["s"] == 42
    assert result["sr"] == 180
    assert result["w"] == 17
    assert result["wr"] == 6240
    assert result["st"] == "allowed"


@pytest.mark.asyncio
async def test_poll_builds_payload_with_proxy(tmp_path):
    creds = tmp_path / "creds.json"
    creds.write_text(json.dumps({"accessToken": "tok-test"}))
    svc = ClaudeService(credentials_file=str(creds), proxy_url="http://127.0.0.1:7890")

    mock_result = MagicMock()
    mock_result.stdout = "HTTP/2 200\r\nanthropic-ratelimit-unified-5h-status: allowed\r\n\r\n"

    with patch("daemon.services.claude.subprocess.run", return_value=mock_result) as mock_run:
        with patch("daemon.services.claude.time.time", return_value=1_000_000):
            await svc.poll()

    cmd = mock_run.call_args[0][0]
    assert "--proxy" in cmd
    assert "http://127.0.0.1:7890" in cmd


@pytest.mark.asyncio
async def test_poll_returns_none_on_api_error(tmp_path):
    creds = tmp_path / "creds.json"
    creds.write_text(json.dumps({"accessToken": "tok-test"}))
    svc = ClaudeService(credentials_file=str(creds))

    with patch("daemon.services.claude.subprocess.run", side_effect=Exception("network error")):
        result = await svc.poll()

    assert result is None
