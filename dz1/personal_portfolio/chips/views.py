from django.shortcuts import render
from .models import Chips

def index(request):
    projects = Chips.objects.all()
    return render(request, 'chips/index.html',{'projects':projects})
