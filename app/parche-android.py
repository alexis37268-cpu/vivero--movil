# Firma fija para que el APK nuevo se instale encima del anterior (lo corre GitHub al compilar)
import base64
open('android/firma.keystore','wb').write(base64.b64decode(open('../firma.txt').read()))
g='android/app/build.gradle'
gs=open(g,encoding='utf-8').read()
if 'firma.keystore' not in gs:
    gs=gs.replace('android {','''android {
    signingConfigs {
        debug {
            storeFile file("${rootDir}/firma.keystore")
            storePassword "android"
            keyAlias "androiddebugkey"
            keyPassword "android"
        }
    }''',1)
    open(g,'w',encoding='utf-8').write(gs)
print('firma lista')
