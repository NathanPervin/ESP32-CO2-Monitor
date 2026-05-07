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
        indexes = [
            models.Index(fields=['building']),
            models.Index(fields=['building', 'room_number']),
            models.Index(fields=['building', 'room_number', 'mode']),
            models.Index(fields=['building', 'room_number', 'unix_timestamp']),
        ]

    def __str__(self):
        return json.dumps({
        "mode": self.mode,
        "building": self.building,
        "room_number": self.room_number,
        "unix_timestamp": self.unix_timestamp,
        "CO2_ppm": self.co2_ppm,
    })