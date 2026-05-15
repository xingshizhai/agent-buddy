# daemon/tests/test_claude.py
import json
import subprocess
import pytest
from unittest.mock import MagicMock, patch
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


def test_read_token_from_nested_claudeAiOauth(svc, tmp_path, monkeypatch):
    creds = tmp_path / "creds.json"
    creds.write_text(json.dumps({"claudeAiOauth": {"accessToken": "nested-token"}}))
    monkeypatch.setattr("daemon.services.claude.CREDENTIALS_FILE", creds)
    assert svc._read_token() == "nested-token"


@pytest.mark.asyncio
async def test_poll_builds_payload(svc, tmp_path, monkeypatch):
    creds = tmp_path / "creds.json"
    creds.write_text(json.dumps({"accessToken": "tok-test"}))
    monkeypatch.setattr("daemon.services.claude.CREDENTIALS_FILE", creds)

    fixed_now = 1_000_000
    # Simulate curl -D - output with rate-limit headers
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
    assert result["s"] == 42       # round(0.42 * 100)
    assert result["sr"] == 180     # 10800 / 60
    assert result["w"] == 17       # round(0.17 * 100)
    assert result["wr"] == 6240    # 374400 / 60
    assert result["st"] == "allowed"


@pytest.mark.asyncio
async def test_poll_returns_none_on_api_error(svc, tmp_path, monkeypatch):
    creds = tmp_path / "creds.json"
    creds.write_text(json.dumps({"accessToken": "tok-test"}))
    monkeypatch.setattr("daemon.services.claude.CREDENTIALS_FILE", creds)

    with patch("daemon.services.claude.subprocess.run", side_effect=Exception("network error")):
        result = await svc.poll()

    assert result is None
