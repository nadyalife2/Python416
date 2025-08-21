"""
URL configuration for personal_portfolio project.

The `urlpatterns` list routes URLs to views. For more information please see:
    https://docs.djangoproject.com/en/5.2/topics/http/urls/
Examples:
Function views
    1. Add an import:  from my_app import views
    2. Add a URL to urlpatterns:  path('', views.home, name='home')
Class-based views
    1. Add an import:  from other_app.views import Home
    2. Add a URL to urlpatterns:  path('', Home.as_view(), name='home')
Including another URLconf
    1. Import the include() function: from django.urls import include, path
    2. Add a URL to urlpatterns:  path('blog/', include('blog.urls'))
"""
from django.contrib import admin
from django.urls import path, include

from django.conf.urls.static import static
from django.conf import settings
from chips import views as chips_views
from todo import views as todo_views

urlpatterns = [
    path('admin/', admin.site.urls),
    path('', chips_views.index, name='index'),
    path('blog/', include('blog.urls')),
    path('signup/', todo_views.signup_user, name='signupuser'),
    path('current/', todo_views.current_todos, name="currenttodos"),
]

urlpatterns += static(settings.MEDIA_URL, document_root=settings.MEDIA_ROOT)