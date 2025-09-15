from django.db import models




class Project(models.Model):
    title = models.CharField(max_length=200)
    description = models.TextField(null=True, blank=True)
    featured_images = models.ImageField(upload_to="projects/%Y/%m/%d",
                                      blank=True, default ="default.jpg")
    demo_link=models.CharField(max_length=2000, blank=True)
    source_link= models. CharField(max_length=2000, blank=True)
    tags= models.ManyToManyField('Tag', blank=True)
    vote_total= models.IntegerField(default=0, blank=True)
    vote_rating = models.IntegerField(default=0, blank=True)
    created = models.DateTimeField(auto_now_add=True)


    def __str__(self):
        return self.title

class Tag(models.Model):
    name= models.CharField(max_length=200)
    created= models.DateTimeField(auto_now_add=True)



    def __str__(self):
        return self.name