# Genera resources/icon.png y resources/splash.png (lo corre GitHub al compilar)
import os
from PIL import Image, ImageDraw
os.makedirs('resources', exist_ok=True)
S=1024; V=(47,125,74); B=(245,245,240); im=Image.new('RGB',(S,S),V); d=ImageDraw.Draw(im)
# invernadero: techo a dos aguas con ventana de venteo abierta y una hoja
d.polygon([(232,470),(512,250),(792,470)],fill=B)
d.rectangle([272,470,752,770],fill=B)
d.rectangle([312,510,712,730],fill=V)
d.polygon([(512,250),(700,330),(650,395),(512,320)],fill=(200,230,210))   # venteo
d.ellipse([452,560,572,700],fill=B); d.rectangle([506,640,518,730],fill=B)  # hoja
im.save('resources/icon.png')
Image.new('RGB',(2732,2732),V).save('resources/splash.png')
print('iconos listos')
