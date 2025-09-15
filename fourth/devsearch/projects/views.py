from django.shortcuts import render, redirect
from .models import Project
from .forms import ProjectForm

def projects(request):
    pr= Project.objects.all()
    contex = {
        'projects': pr
    }
    return render(request, "projects/projects.html", contex)

def project(request, pk):
    project_obj = Project.objects.get(id=pk)
    return render(request, "projects/single-project.html",
                           {'project': project_obj})


def create_project(request):
    form = ProjectForm()

    if request.method == 'POST':
        form = ProjectForm(request.POST, request.FILES)
        if form.is_valid():
            form.save()
        return redirect('projects')

    return render(request, 'projects/form-template.html', {'form':form})

