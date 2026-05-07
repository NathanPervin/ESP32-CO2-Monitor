import os
import pytest
import requests
from dotenv import load_dotenv
load_dotenv()

BASE_URL = os.environ.get("API_BASE_URL")
ENDPOINT = f"{BASE_URL}/api/log/"
VALID_TOKEN = os.environ.get("CO2_API_TOKEN")

VALID_ENTRY = {
    "mode": "ambient",
    "building": "debug",
    "room_number": "1",
    "unix_timestamp": 1700000000,
    "CO2_ppm": 450,
}


def post(payload, token=None, raw_body=None):
    """Helper to POST to the endpoint."""
    headers = {"Authorization": f"Bearer {VALID_TOKEN}"}
    if token is not None:
        headers["Authorization"] = token
    if raw_body is not None:
        headers["Content-Type"] = "application/json"
        return requests.post(ENDPOINT, data=raw_body, headers=headers)
    return requests.post(ENDPOINT, json=payload, headers=headers)


def drop(d, key):
    """Return a copy of dict d without the given key."""
    return {k: v for k, v in d.items() if k != key}


_ts = 1700100000

def unique_timestamp():
    """Incrementing timestamp spaced far apart to always trigger new sessions."""
    global _ts
    _ts += 1000
    return _ts


# ── Authentication ────────────────────────────────────────────────────────────

class TestAuthentication:

    def test_valid_token_accepted(self):
        r = post({**VALID_ENTRY, "unix_timestamp": unique_timestamp()})
        assert r.status_code == 201
        assert r.json()["success"] is True

    def test_no_token_rejected(self):
        r = post(VALID_ENTRY, token="")
        assert r.status_code == 401

    def test_wrong_token_rejected(self):
        r = post(VALID_ENTRY, token="Bearer wrong-token")
        assert r.status_code == 401

    def test_token_without_bearer_prefix_rejected(self):
        # Sends the raw token value without "Bearer " prefix
        r = post(VALID_ENTRY, token=VALID_TOKEN)
        assert r.status_code == 401


# ── HTTP Method ───────────────────────────────────────────────────────────────

class TestHttpMethod:

    def test_get_rejected(self):
        r = requests.get(ENDPOINT, headers={"Authorization": f"Bearer {VALID_TOKEN}"})
        assert r.status_code == 405

    def test_put_rejected(self):
        r = requests.put(ENDPOINT, headers={"Authorization": f"Bearer {VALID_TOKEN}"})
        assert r.status_code == 405

    def test_patch_rejected(self):
        r = requests.patch(ENDPOINT, headers={"Authorization": f"Bearer {VALID_TOKEN}"})
        assert r.status_code == 405


# ── JSON Parsing ──────────────────────────────────────────────────────────────

class TestJsonParsing:

    def test_invalid_json_rejected(self):
        r = post(None, raw_body="{not valid json}")
        assert r.status_code == 400
        assert "Invalid JSON" in r.json()["error"]

    def test_empty_body_rejected(self):
        r = post(None, raw_body="")
        assert r.status_code == 400

    def test_json_number_rejected(self):
        r = post(None, raw_body="42")
        assert r.status_code == 400

    def test_json_string_rejected(self):
        r = post(None, raw_body='"just a string"')
        assert r.status_code == 400

    def test_null_rejected(self):
        r = post(None, raw_body="null")
        assert r.status_code == 400


# ── Single Object Payload ─────────────────────────────────────────────────────

class TestSingleObject:

    def test_valid_entry_accepted(self):
        r = post({**VALID_ENTRY, "unix_timestamp": unique_timestamp()})
        assert r.status_code == 201
        assert r.json()["records_saved"] == 1

    def test_missing_mode_rejected(self):
        r = post(drop(VALID_ENTRY, "mode"))
        assert r.status_code == 400
        assert "mode" in r.json()["error"]

    def test_missing_building_rejected(self):
        r = post(drop(VALID_ENTRY, "building"))
        assert r.status_code == 400

    def test_missing_room_number_rejected(self):
        r = post(drop(VALID_ENTRY, "room_number"))
        assert r.status_code == 400

    def test_missing_unix_timestamp_rejected(self):
        r = post(drop(VALID_ENTRY, "unix_timestamp"))
        assert r.status_code == 400

    def test_missing_co2_ppm_rejected(self):
        r = post(drop(VALID_ENTRY, "CO2_ppm"))
        assert r.status_code == 400


# ── Array Payload ─────────────────────────────────────────────────────────────

class TestArrayPayload:

    def test_valid_array_accepted(self):
        payload = [
            {**VALID_ENTRY, "unix_timestamp": unique_timestamp()},
            {**VALID_ENTRY, "unix_timestamp": unique_timestamp()},
        ]
        r = post(payload)
        assert r.status_code == 201
        assert r.json()["records_saved"] == 2

    def test_empty_array(self):
        # Should return 201 with 0 records, not crash with a 500
        r = post([])
        assert r.status_code in (200, 201)
        assert r.json().get("records_saved", 0) == 0

    def test_array_with_bad_entry_rejected(self):
        payload = [
            {**VALID_ENTRY, "unix_timestamp": unique_timestamp()},
            drop(VALID_ENTRY, "CO2_ppm"),
        ]
        r = post(payload)
        assert r.status_code == 400


# ── Response Shape ────────────────────────────────────────────────────────────

class TestResponseShape:

    def test_success_response_has_expected_keys(self):
        r = post({**VALID_ENTRY, "unix_timestamp": unique_timestamp()})
        body = r.json()
        assert "success" in body
        assert "records_saved" in body

    def test_error_response_has_error_key(self):
        r = post(drop(VALID_ENTRY, "mode"))
        assert "error" in r.json()

    def test_content_type_is_json(self):
        r = post({**VALID_ENTRY, "unix_timestamp": unique_timestamp()})
        assert "application/json" in r.headers["Content-Type"]