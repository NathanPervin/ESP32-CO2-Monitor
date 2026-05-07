from django.db import models

# Create your models here.
class RoomStats(models.Model):
    building        = models.CharField(max_length=30)
    room_number     = models.CharField(max_length=10)

    session_ids     = models.JSONField(default=list)  # list of all session IDs for this room

    sample_count    = models.IntegerField()           # total readings across all sessions

    co2_min         = models.IntegerField()
    co2_max         = models.IntegerField()
    co2_avg         = models.FloatField()

    room_score      = models.FloatField(null=True, blank=True)

    last_calculated = models.DateTimeField(auto_now=True)

    class Meta:
        unique_together = [['building', 'room_number']]
        indexes = [
            models.Index(fields=['building', 'room_number']),
        ]

    def __str__(self):
        return f"{self.building} {self.room_number}"
