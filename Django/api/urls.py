from django.urls import path
from . import views

urlpatterns = [
    path("log/", views.log_co2, name="log_co2"),
]