from django.shortcuts import render
from django.contrib.auth.decorators import login_required
from django.views.decorators.http import require_POST
from api.models import CO2Reading
from dashboard.models import RoomStats
from datetime import datetime, timezone
from django.db.models import Min, Max, Count
from django.http import JsonResponse
from django.conf import settings
from django.db.models.expressions import RawSQL
from dashboard.management.commands.calculate_room_stats import calculate_room_stats
import time

POST_INTERVAL = settings.SESSION_GAP_INTERVAL
SESSION_GAP_INTERVAL = settings.SESSION_GAP_INTERVAL
MAX_PLOT_POINTS = settings.MAX_PLOT_POINTS

# Create your views here.
@login_required(login_url='/users/login_user')
def dashboard(request):
    session = (CO2Reading.objects
        .values('building', 'room_number', 'mode', 'session_id')
        .annotate(
            start=Min('unix_timestamp'),
            end=Max('unix_timestamp'),
            count=Count('id'),
        )
        .order_by('-end')[:1])

    if not session:
        return render(request, 'dashboard/dashboard.html', {'has_data': False})

    session = session[0]
    start = session['start']
    end = session['end']
    session_hours, session_minutes, session_seconds = get_hours_mins_seconds(end=end, start=start)

    # get the rooms with the highest and lowest average CO2 levels
    # exclude debug entries used for esp32 testing (see README)
    room_qs     = RoomStats.objects.exclude(building__iexact='debug')
    best_rooms  = list(room_qs.order_by('co2_avg')[:5])
    best_ids    = [r.id for r in best_rooms]
    worst_rooms = room_qs.exclude(id__in=best_ids).order_by('-co2_avg')[:5]

    return render(request, 'dashboard/dashboard.html', {
        'has_data':    True,
        'session_id':  session['session_id'],
        'building':    session['building'],
        'room':        session['room_number'],
        'mode':        session['mode'],
        'start_fmt':   datetime.fromtimestamp(start, tz=timezone.utc).strftime('%Y-%m-%d %H:%M:%S'),
        'end_fmt':     datetime.fromtimestamp(end, tz=timezone.utc).strftime('%Y-%m-%d %H:%M:%S'),
        'duration':    f"{int(session_hours)}h {int(session_minutes)}m {int(session_seconds)}s",
        'count':       session['count'],
        'live':        is_live(latest_ts=end),
        'best_rooms':  best_rooms,
        'worst_rooms': worst_rooms,
    })

@login_required(login_url='/users/login_user')  # redirects here if not logged in
def co2_table(request):
    readings = CO2Reading.objects.order_by("-unix_timestamp")[:100]
    return render(request, "dashboard/co2_table.html", {"readings": readings})

@login_required(login_url='/users/login_user')  # redirects here if not logged in
def search(request):

    # get the building and room number from user's GET request after pressing search button
    building = request.GET.get('building', '').strip()
    room = request.GET.get('room', '').strip()

    results = None
    qs = CO2Reading.objects # query set to build the building and room filter
    
    # Add building and room number filters, one or the other is fine 
    if building:
        qs = qs.filter(building__icontains=building)
    if room:
        qs = qs.filter(room_number__icontains=room)
    
    # if nothing is entered for building or room, results will be None
    # and the html table won't be displayed, otherwise results
    # will have the database entries for user's filter
    if building or room:
        rows = (qs.values('building', 'room_number') # return these three values
                  .distinct() # remove duplicates for the many data points
                  .annotate(latest_ts=Max('unix_timestamp'))
                  .order_by('building', 'room_number')) # sort by building, then room number

        results = []
        for r in rows:
            r['is_live'] = is_live(latest_ts=r['latest_ts'])
            results.append(r)


    return render(request, 'dashboard/search.html', {
        'results': results,
        'q_building': building,
        'q_room': room,
    })

@login_required(login_url='/users/login_user')  # redirects here if not logged in
def sessions(request, building, room):

    # get the filter fields entered by the user
    mode      = request.GET.get('mode', '')
    date_from = request.GET.get('date_from', '')
    date_to   = request.GET.get('date_to', '')

    # filter should start by containing the chosen building and room number
    qs = CO2Reading.objects.filter(building=building, room_number=room)

    if mode:
        qs = qs.filter(mode=mode)

    # Get the start and end date and apply them to the filter
    if date_from:
        try:
            dt = datetime.strptime(date_from, '%Y-%m-%d')
            qs = qs.filter(unix_timestamp__gte=int(dt.timestamp()))
        except ValueError: # only can occur if date in url is entered manually, UI entry via flatpickr are valid
            pass
    if date_to:
        try:
            dt = datetime.strptime(date_to, '%Y-%m-%d').replace(hour=23, minute=59, second=59)
            qs = qs.filter(unix_timestamp__lte=int(dt.timestamp()))
        except ValueError: # only can occur if date in url is entered manually, UI entry via flatpickr are valid
            pass

    raw_sessions = (qs
                    .values('mode', 'session_id') 
                    .annotate( # adds additional return key-values for the start time, end time, and number of samples
                        start=Min('unix_timestamp'),
                        end=Max('unix_timestamp'),
                        count=Count('id'),
                    )
                    .order_by('-start')) # sorts by most recent time

    latest_for_room = (CO2Reading.objects
                       .filter(building=building, room_number=room)
                       .order_by('-unix_timestamp')
                       .values_list('unix_timestamp', flat=True)
                       .first())
    device_is_live = is_live(latest_ts=latest_for_room)
    sessions = []
    for session in raw_sessions:

        session_hours, session_minutes, session_seconds = get_hours_mins_seconds(end=session['end'], start=session['start'])

        # save current session + new info to sessions list
        sessions.append({
        'mode':        session['mode'],
        'session_id':  session['session_id'],
        'building':    building,
        'room_number': room,
        'start':       session['start'],
        'end':         session['end'],
        'count':       session['count'],
        'start_fmt':   datetime.fromtimestamp(session['start'], tz=timezone.utc).strftime('%Y-%m-%d %H:%M:%S'),
        'end_fmt':     datetime.fromtimestamp(session['end'], tz=timezone.utc).strftime('%Y-%m-%d %H:%M:%S'),
        'duration':    f"{int(session_hours)}h {int(session_minutes)}m {int(session_seconds)}s",
        'is_live':     device_is_live and not sessions, # only the first (most recent) session can be live
        })

    return render(request, 'dashboard/sessions.html', {
        'building':        building,
        'room':            room,
        'sessions':        sessions,
        'selected_mode':   mode,
        'date_from':       date_from,
        'date_to':         date_to,
    })

@login_required(login_url='/users/login_user')
def plot(request, session_id):

    # get useful data for the plot such as building, room number
    # and timestamp start/stop/quantity
    session = (CO2Reading.objects
        .filter(session_id=session_id)
        .values('building', 'room_number', 'mode')
        .annotate(
            start=Min('unix_timestamp'),
            end=Max('unix_timestamp'),
            count=Count('id'),
        )
        .order_by()[:1])

    # check that db query was successful before continuing
    if not session:
        return render(request, 'dashboard/plot.html', {'has_data': False})
    else:
        session = session[0]

    start = session['start']
    end = session['end']
    session_hours, session_minutes, session_seconds = get_hours_mins_seconds(end=end, start=start)

    return render(request, 'dashboard/plot.html', {
        'has_data':   True,
        'session_id': session_id,
        'building':   session['building'],
        'room':       session['room_number'],
        'mode':       session['mode'],
        'start_fmt':  datetime.fromtimestamp(start, tz=timezone.utc).strftime('%Y-%m-%d %H:%M:%S'),
        'end_fmt':    datetime.fromtimestamp(end, tz=timezone.utc).strftime('%Y-%m-%d %H:%M:%S'),
        'duration':   f"{int(session_hours)}h {int(session_minutes)}m {int(session_seconds)}s",
        'count':      session['count'],
        'live':       is_live(latest_ts=end),
    })


@login_required(login_url='/users/login_user')
def get_session_data(request, session_id):

    # specify query set to a specific session id and order in oldest timestamp first
    qs = CO2Reading.objects
    qs = qs.filter(session_id=session_id)
    qs = qs.order_by('unix_timestamp')

    # get the number of rows in the query set
    total_points_in_session = qs.count()

    # downsample if needed (filter database to retrieve every every nth row)
    if total_points_in_session > MAX_PLOT_POINTS:
        step = max(1, total_points_in_session // MAX_PLOT_POINTS)
        qs = qs.annotate(mod_val=RawSQL("unix_timestamp %% %s", (step,))) \
       .filter(mod_val=0) \
       .order_by('unix_timestamp')[:MAX_PLOT_POINTS]

    data = list(qs.values_list('unix_timestamp', 'co2_ppm'))
    return JsonResponse({
        'timestamps': [d[0] for d in data],
        'co2':        [d[1] for d in data],
    })

# Called when user recalculates the room statistics,
# modifies the RoomStats table
@login_required(login_url='/users/login_user')
@require_POST
def run_calculate_room_stats(request):
    updated, skipped = calculate_room_stats()
    return JsonResponse({'updated': updated, 'skipped': skipped})

def get_hours_mins_seconds(end, start):
    # subtract the start unix timestamp from the end unix timestamp
    # this gives the duration of the session in seconds
    total_seconds = end - start

    # divide by 3600 seconds to get the number of hours, the remainer is the number of seconds
    session_duration_hours, remainder = divmod(total_seconds, 3600)

    # divide the number of seconds after the hours by 60 to find the number of minutes, 
    # the remainder is the number of seconds
    session_duration_minutes, session_duration_seconds = divmod(remainder, 60)

    return session_duration_hours, session_duration_minutes, session_duration_seconds

"""
Determines if the data is currently being sent. Checks if the most recent timestamp
in the database is less than the interval of data points being sent.
"""
def is_live(latest_ts=None):
    if latest_ts is None:
        latest_ts = CO2Reading.objects.order_by('-unix_timestamp').values_list('unix_timestamp', flat=True).first()
    if not latest_ts:
        return False
    return (time.time() - latest_ts) < SESSION_GAP_INTERVAL