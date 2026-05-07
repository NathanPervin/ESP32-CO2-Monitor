from django.shortcuts import render, redirect
from django.contrib.auth import authenticate, login, logout
from django.contrib import messages
from django.contrib.auth.decorators import login_required
from django.views.decorators.http import require_POST

# Create your views here.
def login_user(request):

    # if the user is already logged in, skip the login page
    if request.user.is_authenticated:
        return redirect('dashboard')
    
    if request.method == "POST": # if the user fills out the login form
        username = request.POST["username"]
        password = request.POST["password"]
        user = authenticate(request, username=username, password=password)
        if user is not None:
            login(request, user)
            return redirect('dashboard')
        else:
            messages.error(request, ("There Was An Error Logging In, Try Again..."))
            return render(request, 'authenticate/login.html', {})
    else: # if the user goes to the web page but doesn't fill out the login form
        return render(request, 'authenticate/login.html', {})

@require_POST
def logout_user(request):
    logout(request)
    return redirect('login')