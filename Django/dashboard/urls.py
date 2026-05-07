from django.urls import path
from . import views

urlpatterns = [
    path('', views.dashboard, name='dashboard'),
    path("table/", views.co2_table, name="co2_table"),
    path('search/', views.search, name='search'),
    path('sessions/<str:building>/<str:room>/', views.sessions, name='sessions'),
    path('plot/<str:session_id>/', views.plot, name='plot'),
    path('plot/<str:session_id>/data/', views.get_session_data, name='get_session_data'),
    path('calculate-room-stats/', views.run_calculate_room_stats, name='run_calculate_room_stats'),
]