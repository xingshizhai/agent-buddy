# daemon/tests/test_claude.py
import json
import pytest
from unittest.mock import AsyncMock, MagicMock, patch
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


@pytest.mark.asyncio
async def test_read_token_from_nested_claudeAiOauth(svc, tmp_path, monkeypatch):
    creds = tmp_path / "creds.json"
    creds.write_text(json.dumps({"claudeAiOauth": {"accessToken": "nested-token"}}))
    monkeypatch.setattr("daemon.services.claude.CREDENTIALS_FILE", creds)
    token = svc._read_token()
    assert token == "nested-token"


@pytest.mark.asyncio
async def test_poll_builds_payload(svc, tmp_path, monkeypatch):
    creds = tmp_path / "creds.json"
    creds.write_text(json.dumps({"accessToken": "tok-test"}))
    monkeypatch.setattr("daemon.services.claude.CREDENTIALS_FILE", creds)

    fixed_now = 1_000_000
    mock_headers = {
        "anthropic-ratelimit-unified-5h-utilization": "0.42",
        "anthropic-ratelimit-unified-5h-reset": str(fixed_now + 10_800),
        "anthropic-ratelimit-unified-7d-utilization": "0.17",
        "anthropic-ratelimit-unified-7d-reset": str(fixed_now + 374_400),
        "anthropic-ratelimit-unified-5h-status": "allowed",
    }

    mock_resp = AsyncMock()
    mock_resp.headers = mock_headers
    mock_resp.__aenter__ = AsyncMock(return_value=mock_resp)
    mock_resp.__aexit__ = AsyncMock(return_value=False)

    mock_session = AsyncMock()
    mock_session.post = MagicMock(return_value=mock_resp)
    mock_session.__aenter__ = AsyncMock(return_value=mock_session)
    mock_session.__aexit__ = AsyncMock(return_value=False)

    with patch("daemon.services.claude.aiohttp.ClientSession", return_value=mock_session):
        with patch("daemon.services.claude.time.time", return_value=fixed_now):
            result = await svc.poll()

    assert result is not None
    assert result["s"] == 42
    assert result["sr"] == 180
    assert result["w"] == 17
    assert result["wr"] == 6240
    assert result["st"] == "allowed"


@pytest.mark.asyncio
async def test_poll_returns_none_on_api_error(svc, tmp_path, monkeypatch):
    creds = tmp_path / "creds.json"
    creds.write_text(json.dumps({"accessToken": "tok-test"}))
    monkeypatch.setattr("daemon.services.claude.CREDENTIALS_FILE", creds)

    mock_session = AsyncMock()
    mock_session.post = MagicMock(side_effect=Exception("network error"))
    mock_session.__aenter__ = AsyncMock(return_value=mock_session)
    mock_session.__aexit__ = AsyncMock(return_value=False)

    with patch("daemon.services.claude.aiohttp.ClientSession", return_value=mock_session):
        result = await svc.poll()

    assert result is None
