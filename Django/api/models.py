from django.db import models
import json

# Create your models here.
class CO2Reading(models.Model):
    mode = models.CharField(max_length=50)
    building = models.CharField(max_length=30)
    room_number = models.CharField(max_length=10)
    unix_timestamp = models.BigIntegerField()
    co2_ppm = models.IntegerField()
    session_id = models.CharField(max_length=50, null=True, blank=True)

    class Meta:
        # leading columns of an index can also be used on their own, so (building) and
        # (building, room_number) lookups are served by the longer indexes below
        indexes = [
            models.Index(fields=['building', 'room_number', 'unix_timestamp']),
            models.Index(fields=['building', 'room_number', 'mode', 'unix_timestamp']),  # get_session_id() on every POST
            models.Index(fields=['building', 'room_number', 'session_id', 'mode', 'unix_timestamp']),  # sessions list
            models.Index(fields=['session_id', 'unix_timestamp', 'co2_ppm']),  # plot page + plot data
            models.Index(fields=['unix_timestamp']),  # most recent reading (dashboard)
        ]

    def __str__(self):
        return json.dumps({
        "mode": self.mode,
        "building": self.building,
        "room_number": self.room_number,
        "unix_timestamp": self.unix_timestamp,
        "CO2_ppm": self.co2_ppm,
    })

class CO2Session(models.Model):
    session_id = models.CharField(max_length=50, unique=True)
    mode = models.CharField(max_length=50)
    building = models.CharField(max_length=30)
    room_number = models.CharField(max_length=10)
    start_timestamp = models.BigIntegerField()  # oldest timestamp in the session
    end_timestamp = models.BigIntegerField()    # newest timestamp in the session
    reading_count = models.IntegerField(default=0)

    class Meta:
        indexes = [
            models.Index(fields=['building', 'room_number', 'start_timestamp']),
        ]

    def __str__(self):
        return f"{self.building} {self.room_number} {self.session_id}"