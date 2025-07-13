from django.db import models

class Chips(models.Model):
     title=models.CharField(max_length=120)
     description= models.CharField(max_length=300)
     url= models.URLField(blank=True)