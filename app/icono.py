# Genera resources/icon.png y resources/splash.png con el logo de Sol Puntano (lo corre GitHub al compilar)
import os
from PIL import Image
os.makedirs('resources', exist_ok=True)
sim=Image.open('logo-simbolo.png').convert('RGB')     # sol + planta + libro (sin letras)
S=1024; ic=Image.new('RGB',(S,S),'white')
tw=int(S*0.62); th=int(sim.height*tw/sim.width)       # dentro de la zona segura del icono de Android
ic.paste(sim.resize((tw,th),Image.LANCZOS),((S-tw)//2,(S-th)//2))
ic.save('resources/icon.png')
log=Image.open('logo-completo.png').convert('RGB')    # logo completo para la pantalla de inicio
W=2732; sp=Image.new('RGB',(W,W),'white')
lw=int(W*0.45); lh=int(log.height*lw/log.width)
sp.paste(log.resize((lw,lh),Image.LANCZOS),((W-lw)//2,(W-lh)//2))
sp.save('resources/splash.png')
print('iconos listos')
