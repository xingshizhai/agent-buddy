# daemon/tests/test_kimi.py
import json
import pytest
from unittest.mock import MagicMock, patch
from daemon.services.kimi import KimiService, _parse_jwt_claims, _iso_to_reset_mins

FAKE_JWT = (
    "eyJhbGciOiJIUzUxMiJ9"
    ".eyJzdWIiOiJ1c2VyMTIzIiwic3NpZCI6InNzaWQ5OTkiLCJkZXZpY2VfaWQiOiJkZXY0NTYifQ"
    ".fakesig"
)


def test_parse_jwt_claims():
    claims = _parse_jwt_claims(FAKE_JWT)
    assert claims["sub"] == "user123"
    assert claims["ssid"] == "ssid999"
    assert claims["device_id"] == "dev456"


def test_iso_to_reset_mins():
    import time as _time
    now = int(_time.time())
    future = now + 3600  # 1 hour from now
    from datetime import datetime, timezone, timedelta
    iso = datetime.fromtimestamp(future, tz=timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    result = _iso_to_reset_mins(iso, now)
    assert abs(result - 60) <= 1   # ≈ 60 minutes
    assert _iso_to_reset_mins("invalid", 0) == -1
    past_iso = datetime.fromtimestamp(now - 100, tz=timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    assert _iso_to_reset_mins(past_iso, now) == 0


def test_service_id():
    svc = KimiService(auth_token=FAKE_JWT)
    assert svc.service_id == "kimi"


def test_poll_interval_default():
    svc = KimiService(auth_token=FAKE_JWT)
    assert svc.poll_interval == 300


def test_poll_interval_custom():
    svc = KimiService(auth_token=FAKE_JWT, poll_interval=600)
    assert svc.poll_interval == 600


@pytest.mark.asyncio
async def test_poll_returns_none_without_token():
    svc = KimiService(auth_token="")
    # No token file either → returns None
    svc._token = ""
    result = await svc.poll()
    assert result is None


@pytest.mark.asyncio
async def test_poll_returns_none_on_unauthenticated(tmp_path):
    svc = KimiService(auth_token=FAKE_JWT)
    mock = MagicMock()
    mock.stdout = json.dumps({"code": "unauthenticated"})
    with patch("daemon.services.kimi.subprocess.run", return_value=mock):
        result = await svc.poll()
    assert result is None


@pytest.mark.asyncio
async def test_poll_builds_payload(tmp_path):
    svc = KimiService(auth_token=FAKE_JWT)

    fake_response = {
        "usages": [{
            "scope": "FEATURE_CODING",
            "detail": {
                "limit": "100",
                "used": "30",
                "remaining": "70",
                "resetTime": "2026-05-16T15:00:00Z",
            },
            "limits": [{
                "window": {"duration": 300, "timeUnit": "TIME_UNIT_MINUTE"},
                "detail": {
                    "limit": "100",
                    "remaining": "85",
                    "resetTime": "2026-05-15T19:00:00Z",
                },
            }],
        }],
    }
    mock = MagicMock()
    mock.stdout = json.dumps(fake_response)

    fixed_now = 1_747_400_000  # arbitrary fixed time
    with patch("daemon.services.kimi.subprocess.run", return_value=mock):
        with patch("daemon.services.kimi.time.time", return_value=fixed_now):
            result = await svc.poll()

    assert result is not None
    assert result["w"] == 30          # 30/100
    assert result["s"] == 15          # (100-85)/100
    assert result["st"] == "allowed"


@pytest.mark.asyncio
async def test_poll_returns_none_on_api_error():
    svc = KimiService(auth_token=FAKE_JWT)
    with patch("daemon.services.kimi.subprocess.run", side_effect=Exception("timeout")):
        result = await svc.poll()
    assert result is None
