import json
from django.test import TestCase, Client
from django.conf import settings
from .models import CO2Reading

VALID_ENTRY = {
    "mode": "ambient",
    "building": "debug",
    "room_number": "1",
    "unix_timestamp": 1700000000,
    "CO2_ppm": 450,
}

def drop(d, key):
    return {k: v for k, v in d.items() if k != key}

_ts = 1700100000
def unique_ts():
    global _ts
    _ts += 1000
    return _ts


class AuthenticationTest(TestCase):

    def setUp(self):
        self.client = Client()
        self.url = "/api/log/"
        self.token = settings.CO2_API_TOKEN

    def post(self, data, token=None):
        auth = token if token is not None else f"Bearer {self.token}"
        return self.client.post(self.url, data=json.dumps(data),
                                content_type="application/json",
                                HTTP_AUTHORIZATION=auth)

    def test_valid_token_accepted(self):
        r = self.post({**VALID_ENTRY, "unix_timestamp": unique_ts()})
        self.assertEqual(r.status_code, 201)
        self.assertTrue(r.json()["success"])

    def test_no_token_rejected(self):
        r = self.post(VALID_ENTRY, token="")
        self.assertEqual(r.status_code, 401)

    def test_wrong_token_rejected(self):
        r = self.post(VALID_ENTRY, token="Bearer wrong-token")
        self.assertEqual(r.status_code, 401)

    def test_token_without_bearer_prefix_rejected(self):
        r = self.post(VALID_ENTRY, token=self.token)
        self.assertEqual(r.status_code, 401)

    def test_lowercase_bearer_rejected(self):
        r = self.post(VALID_ENTRY, token=f"bearer {self.token}")
        self.assertEqual(r.status_code, 401)

    def test_bearer_prefix_only_rejected(self):
        r = self.post(VALID_ENTRY, token="Bearer ")
        self.assertEqual(r.status_code, 401)

    def test_bearer_glued_to_token_rejected(self):
        r = self.post(VALID_ENTRY, token=f"Bearer{self.token}")
        self.assertEqual(r.status_code, 401)

    def test_double_space_after_bearer_rejected(self):
        r = self.post(VALID_ENTRY, token=f"Bearer  {self.token}")
        self.assertEqual(r.status_code, 401)


class HttpMethodTest(TestCase):

    def setUp(self):
        self.client = Client()
        self.url = "/api/log/"
        self.auth = {"HTTP_AUTHORIZATION": f"Bearer {settings.CO2_API_TOKEN}"}

    def test_get_rejected(self):
        r = self.client.get(self.url, **self.auth)
        self.assertEqual(r.status_code, 405)

    def test_put_rejected(self):
        r = self.client.put(self.url, **self.auth)
        self.assertEqual(r.status_code, 405)

    def test_patch_rejected(self):
        r = self.client.patch(self.url, **self.auth)
        self.assertEqual(r.status_code, 405)

    def test_delete_rejected(self):
        r = self.client.delete(self.url, **self.auth)
        self.assertEqual(r.status_code, 405)


class JsonParsingTest(TestCase):

    def setUp(self):
        self.client = Client()
        self.url = "/api/log/"
        self.auth = f"Bearer {settings.CO2_API_TOKEN}"

    def raw_post(self, body):
        return self.client.post(self.url, data=body, content_type="application/json",
                                HTTP_AUTHORIZATION=self.auth)

    def test_invalid_json_rejected(self):
        r = self.raw_post("{not valid json}")
        self.assertEqual(r.status_code, 400)
        self.assertIn("Invalid JSON", r.json()["error"])

    def test_empty_body_rejected(self):
        r = self.raw_post("")
        self.assertEqual(r.status_code, 400)

    def test_json_number_rejected(self):
        r = self.raw_post("42")
        self.assertEqual(r.status_code, 400)

    def test_json_string_rejected(self):
        r = self.raw_post('"just a string"')
        self.assertEqual(r.status_code, 400)

    def test_null_rejected(self):
        r = self.raw_post("null")
        self.assertEqual(r.status_code, 400)


class SingleObjectTest(TestCase):

    def setUp(self):
        self.client = Client()
        self.url = "/api/log/"
        self.token = settings.CO2_API_TOKEN

    def post(self, data):
        return self.client.post(self.url, data=json.dumps(data),
                                content_type="application/json",
                                HTTP_AUTHORIZATION=f"Bearer {self.token}")

    def test_valid_entry_accepted(self):
        r = self.post({**VALID_ENTRY, "unix_timestamp": unique_ts()})
        self.assertEqual(r.status_code, 201)
        self.assertEqual(r.json()["records_saved"], 1)

    def test_missing_mode_rejected(self):
        r = self.post(drop(VALID_ENTRY, "mode"))
        self.assertEqual(r.status_code, 400)
        self.assertIn("mode", r.json()["error"])

    def test_missing_building_rejected(self):
        r = self.post(drop(VALID_ENTRY, "building"))
        self.assertEqual(r.status_code, 400)

    def test_missing_room_number_rejected(self):
        r = self.post(drop(VALID_ENTRY, "room_number"))
        self.assertEqual(r.status_code, 400)

    def test_missing_unix_timestamp_rejected(self):
        r = self.post(drop(VALID_ENTRY, "unix_timestamp"))
        self.assertEqual(r.status_code, 400)

    def test_missing_co2_ppm_rejected(self):
        r = self.post(drop(VALID_ENTRY, "CO2_ppm"))
        self.assertEqual(r.status_code, 400)

    def test_extra_fields_are_ignored(self):
        r = self.post({**VALID_ENTRY, "unix_timestamp": unique_ts(), "unexpected_key": "ignored"})
        self.assertEqual(r.status_code, 201)
        self.assertEqual(r.json()["records_saved"], 1)


class ArrayPayloadTest(TestCase):

    def setUp(self):
        self.client = Client()
        self.url = "/api/log/"
        self.token = settings.CO2_API_TOKEN

    def post(self, data):
        return self.client.post(self.url, data=json.dumps(data),
                                content_type="application/json",
                                HTTP_AUTHORIZATION=f"Bearer {self.token}")

    def test_valid_array_accepted(self):
        payload = [
            {**VALID_ENTRY, "unix_timestamp": unique_ts()},
            {**VALID_ENTRY, "unix_timestamp": unique_ts()},
        ]
        r = self.post(payload)
        self.assertEqual(r.status_code, 201)
        self.assertEqual(r.json()["records_saved"], 2)

    def test_empty_array(self):
        r = self.post([])
        self.assertIn(r.status_code, (200, 201))
        self.assertEqual(r.json()["records_saved"], 0)

    def test_single_element_array_accepted(self):
        r = self.post([{**VALID_ENTRY, "unix_timestamp": unique_ts()}])
        self.assertEqual(r.status_code, 201)
        self.assertEqual(r.json()["records_saved"], 1)

    def test_large_batch_accepted(self):
        batch = [{**VALID_ENTRY, "unix_timestamp": unique_ts()} for _ in range(50)]
        r = self.post(batch)
        self.assertEqual(r.status_code, 201)
        self.assertEqual(r.json()["records_saved"], 50)

    def test_duplicate_timestamps_both_saved(self):
        # No unique constraint on timestamp; both rows must persist
        ts = unique_ts()
        r = self.post([
            {**VALID_ENTRY, "unix_timestamp": ts},
            {**VALID_ENTRY, "unix_timestamp": ts},
        ])
        self.assertEqual(r.status_code, 201)
        self.assertEqual(r.json()["records_saved"], 2)

    def test_bad_field_in_second_entry_rejects_whole_batch(self):
        r = self.post([
            {**VALID_ENTRY, "unix_timestamp": unique_ts()},
            drop(VALID_ENTRY, "unix_timestamp"),
        ])
        self.assertEqual(r.status_code, 400)


class FieldValuesTest(TestCase):

    def setUp(self):
        self.client = Client()
        self.url = "/api/log/"
        self.token = settings.CO2_API_TOKEN

    def post(self, data):
        return self.client.post(self.url, data=json.dumps(data),
                                content_type="application/json",
                                HTTP_AUTHORIZATION=f"Bearer {self.token}")

    def test_zero_co2_ppm_accepted(self):
        r = self.post({**VALID_ENTRY, "unix_timestamp": unique_ts(), "CO2_ppm": 0})
        self.assertEqual(r.status_code, 201)

    def test_negative_co2_ppm_accepted(self):
        # No lower-bound validation in the view
        r = self.post({**VALID_ENTRY, "unix_timestamp": unique_ts(), "CO2_ppm": -50})
        self.assertEqual(r.status_code, 201)

    def test_very_high_co2_ppm_accepted(self):
        r = self.post({**VALID_ENTRY, "unix_timestamp": unique_ts(), "CO2_ppm": 9999})
        self.assertEqual(r.status_code, 201)


class ResponseShapeTest(TestCase):

    def setUp(self):
        self.client = Client()
        self.url = "/api/log/"
        self.token = settings.CO2_API_TOKEN

    def post(self, data):
        return self.client.post(self.url, data=json.dumps(data),
                                content_type="application/json",
                                HTTP_AUTHORIZATION=f"Bearer {self.token}")

    def test_success_response_has_expected_keys(self):
        r = self.post({**VALID_ENTRY, "unix_timestamp": unique_ts()})
        body = r.json()
        self.assertIn("success", body)
        self.assertIn("records_saved", body)

    def test_error_response_has_error_key(self):
        r = self.post(drop(VALID_ENTRY, "mode"))
        self.assertIn("error", r.json())

    def test_content_type_is_json(self):
        r = self.post({**VALID_ENTRY, "unix_timestamp": unique_ts()})
        self.assertIn("application/json", r["Content-Type"])


class SessionContinuityTest(TestCase):
    """
    SESSION_GAP_INTERVAL = 300 s. Verifies the session-detection logic in
    get_session_id() assigns session IDs correctly based on timestamp gaps.
    """

    def setUp(self):
        self.client = Client()
        self.url = "/api/log/"
        self.token = settings.CO2_API_TOKEN

    def post(self, data):
        return self.client.post(self.url, data=json.dumps(data),
                                content_type="application/json",
                                HTTP_AUTHORIZATION=f"Bearer {self.token}")

    def get_session_id(self, ts):
        return CO2Reading.objects.filter(unix_timestamp=ts).values_list("session_id", flat=True).first()

    def test_first_reading_creates_new_session(self):
        ts = unique_ts()
        r = self.post({**VALID_ENTRY, "unix_timestamp": ts})
        self.assertEqual(r.status_code, 201)
        self.assertIsNotNone(self.get_session_id(ts))

    def test_timestamp_within_gap_continues_session(self):
        # 299 s apart — inside the 300 s gap, should reuse session
        ts = unique_ts()
        self.post({**VALID_ENTRY, "unix_timestamp": ts})
        self.post({**VALID_ENTRY, "unix_timestamp": ts + 299})
        sid1 = self.get_session_id(ts)
        sid2 = self.get_session_id(ts + 299)
        self.assertEqual(sid1, sid2)

    def test_timestamp_beyond_gap_starts_new_session(self):
        # 301 s apart — outside the 300 s gap, should create new session
        ts = unique_ts()
        self.post({**VALID_ENTRY, "unix_timestamp": ts})
        self.post({**VALID_ENTRY, "unix_timestamp": ts + 301})
        sid1 = self.get_session_id(ts)
        sid2 = self.get_session_id(ts + 301)
        self.assertNotEqual(sid1, sid2)

    def test_different_rooms_get_independent_sessions(self):
        # Same timestamp, different rooms — each starts its own session
        ts = unique_ts()
        self.post({**VALID_ENTRY, "unix_timestamp": ts, "room_number": "101"})
        self.post({**VALID_ENTRY, "unix_timestamp": ts, "room_number": "102"})
        sid1 = CO2Reading.objects.filter(unix_timestamp=ts, room_number="101").values_list("session_id", flat=True).first()
        sid2 = CO2Reading.objects.filter(unix_timestamp=ts, room_number="102").values_list("session_id", flat=True).first()
        self.assertNotEqual(sid1, sid2)

    def test_different_buildings_get_independent_sessions(self):
        ts = unique_ts()
        self.post({**VALID_ENTRY, "unix_timestamp": ts, "building": "BuildingA"})
        self.post({**VALID_ENTRY, "unix_timestamp": ts, "building": "BuildingB"})
        sid1 = CO2Reading.objects.filter(unix_timestamp=ts, building="BuildingA").values_list("session_id", flat=True).first()
        sid2 = CO2Reading.objects.filter(unix_timestamp=ts, building="BuildingB").values_list("session_id", flat=True).first()
        self.assertNotEqual(sid1, sid2)
