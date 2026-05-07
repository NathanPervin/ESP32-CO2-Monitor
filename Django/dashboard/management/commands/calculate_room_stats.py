from django.core.management.base import BaseCommand
from django.db.models import Min, Max, Avg, Count
from django.utils import timezone
from api.models import CO2Reading
from dashboard.models import RoomStats


def calculate_room_stats():
    # get all distinct building/room combinations
    rooms = (CO2Reading.objects
             .values('building', 'room_number')
             .distinct())

    n_updated_rooms = 0
    n_skipped_rooms = 0

    for room in rooms:
        building    = room['building']
        room_number = room['room_number']

        # check if a stats row already exists in db for this room
        room_in_database = RoomStats.objects.filter(building=building, room_number=room_number).first()

        # if the room is already in the database, check if there is new data that
        # needs to be factored into to the calculations (room needs to be recalculated)
        if room_in_database:
            latest_reading = (CO2Reading.objects
                              .filter(building=building, room_number=room_number)
                              .order_by('-unix_timestamp')
                              .values_list('unix_timestamp', flat=True)
                              .first())
            # get the time that the room stats were last calculated
            # if the latest timestamp came in after the last time the stats were
            # calculated, then the data needs to be recalculated, otherwise we can skip it
            last_calc_ts = room_in_database.last_calculated.timestamp()
            if latest_reading and latest_reading <= last_calc_ts:
                n_skipped_rooms += 1
                continue

        qs = CO2Reading.objects.filter(building=building, room_number=room_number)

        # aggregate stats across all readings for this room
        stats = qs.aggregate(
            co2_min=Min('co2_ppm'),
            co2_max=Max('co2_ppm'),
            co2_avg=Avg('co2_ppm'),
            sample_count=Count('id'),
        )

        # get all session IDs for the current room
        session_ids = list(
            qs.exclude(session_id__isnull=True)
              .values_list('session_id', flat=True)
              .distinct()
        )

        # Write to database
        RoomStats.objects.update_or_create(
            building=building,
            room_number=room_number,
            defaults={
                'session_ids':  session_ids,
                'sample_count': stats['sample_count'],
                'co2_min':      stats['co2_min'],
                'co2_max':      stats['co2_max'],
                'co2_avg':      round(stats['co2_avg'], 2),
                'room_score':   None,  
            }
        )
        n_updated_rooms += 1

    return n_updated_rooms, n_skipped_rooms


# allow for run via 'python manage.py calculate_room_stats'
class Command(BaseCommand):
    help = 'Calculate and store CO2 stats for each room'

    def handle(self, *args, **options):
        self.stdout.write('Calculating room stats...')
        n_updated_rooms, n_skipped_rooms = calculate_room_stats()
        self.stdout.write(self.style.SUCCESS(
            f'Done. {n_updated_rooms} room(s) n_updated_rooms, {n_skipped_rooms} n_skipped_rooms (no new data).'
        ))
