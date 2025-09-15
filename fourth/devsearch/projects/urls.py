from django.urls import path
from . import views

urlpatterns = [
    path('', views.projects, name="projects"),
    path('projects/<int:pk>/', views.project, name='project'),
    path('create_project/', views.create_project, name="create_project"),
]

