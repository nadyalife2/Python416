from django.shortcuts import render
from django.contrib.auth.forms import UserCreationForm


def signup_user(request):
    return render(request,
                  'todo/signupuser.html', {'form':
                                           UserCreationForm()})


def current_todos(request):
    return render(request,
                  'todo/currenttodos.html')