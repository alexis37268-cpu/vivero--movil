# Ajustes al proyecto Android que genera Capacitor (lo corre GitHub al compilar)
import base64, os, re

# ---------- 1. Firma fija: el APK nuevo se instala encima del anterior ----------
open('android/firma.keystore', 'wb').write(base64.b64decode(open('../firma.txt').read()))
g = 'android/app/build.gradle'
gs = open(g, encoding='utf-8').read()
if 'firma.keystore' not in gs:
    gs = gs.replace('android {', '''android {
    signingConfigs {
        debug {
            storeFile file("${rootDir}/firma.keystore")
            storePassword "android"
            keyAlias "androiddebugkey"
            keyPassword "android"
        }
    }''', 1)

# ---------- 2. Background Runner: su motor de JavaScript viene como .aar ----------
if 'background-runner' not in gs:
    gs = gs.replace("dirs '../capacitor-cordova-android-plugins/src/main/libs', 'libs'",
                    "dirs '../capacitor-cordova-android-plugins/src/main/libs', 'libs'\n"
                    "        dirs '../../node_modules/@capacitor/background-runner/android/src/main/libs', 'libs'", 1)
    assert 'background-runner' in gs, 'no encontre el bloque flatDir'
open(g, 'w', encoding='utf-8').write(gs)

# ---------- 3. Permisos ----------
m = 'android/app/src/main/AndroidManifest.xml'
ms = open(m, encoding='utf-8').read()
if 'REQUEST_IGNORE_BATTERY_OPTIMIZATIONS' not in ms:
    ms = ms.replace('<manifest xmlns:android="http://schemas.android.com/apk/res/android"',
                    '<manifest xmlns:android="http://schemas.android.com/apk/res/android"\n    xmlns:tools="http://schemas.android.com/tools"', 1)
    ms = ms.replace('<uses-permission android:name="android.permission.INTERNET" />', '''<uses-permission android:name="android.permission.INTERNET" />
    <!-- Avisos de clima con la app cerrada -->
    <uses-permission android:name="android.permission.POST_NOTIFICATIONS" />
    <uses-permission android:name="android.permission.REQUEST_IGNORE_BATTERY_OPTIMIZATIONS" />
    <!-- El plugin de segundo plano trae permisos de ubicacion que NO usamos: se sacan -->
    <uses-permission android:name="android.permission.ACCESS_COARSE_LOCATION" tools:node="remove" />
    <uses-permission android:name="android.permission.ACCESS_FINE_LOCATION" tools:node="remove" />
    <uses-permission android:name="android.permission.ACCESS_BACKGROUND_LOCATION" tools:node="remove" />''', 1)
    open(m, 'w', encoding='utf-8').write(ms)

# ---------- 4. Plugin nativo "Bateria": pedir que Android no frene la app ----------
pkg = 'android/app/src/main/java/com/alexis/viverodomovil'
open(os.path.join(pkg, 'BateriaPlugin.java'), 'w', encoding='utf-8').write(r'''package com.alexis.viverodomovil;

import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.PowerManager;
import android.provider.Settings;
import com.getcapacitor.JSObject;
import com.getcapacitor.Plugin;
import com.getcapacitor.PluginCall;
import com.getcapacitor.PluginMethod;
import com.getcapacitor.annotation.CapacitorPlugin;

// Para que los avisos de clima lleguen con la app cerrada, Android no tiene que
// "dormir" la app para ahorrar bateria. Esto pregunta y abre los carteles del sistema.
@CapacitorPlugin(name = "Bateria")
public class BateriaPlugin extends Plugin {

    private boolean libre() {
        if (Build.VERSION.SDK_INT < 23) return true;
        PowerManager pm = (PowerManager) getContext().getSystemService(Context.POWER_SERVICE);
        return pm != null && pm.isIgnoringBatteryOptimizations(getContext().getPackageName());
    }

    @PluginMethod
    public void estado(PluginCall call) {
        JSObject r = new JSObject();
        r.put("libre", libre());
        r.put("marca", Build.MANUFACTURER == null ? "" : Build.MANUFACTURER.toLowerCase());
        call.resolve(r);
    }

    // Cartel del sistema: "Permitir que la app se ejecute en segundo plano"
    @PluginMethod
    public void pedir(PluginCall call) {
        try {
            if (!libre()) {
                Intent i = new Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS);
                i.setData(Uri.parse("package:" + getContext().getPackageName()));
                i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                getContext().startActivity(i);
            }
        } catch (Exception e) {
            abrirDetalles();
        }
        call.resolve();
    }

    // Algunas marcas tienen ademas su propio "inicio automatico"
    @PluginMethod
    public void autoinicio(PluginCall call) {
        String[][] pantallas = {
            {"com.miui.securitycenter", "com.miui.permcenter.autostart.AutoStartManagementActivity"},
            {"com.coloros.safecenter", "com.coloros.safecenter.permission.startup.StartupAppListActivity"},
            {"com.oppo.safe", "com.oppo.safe.permission.startup.StartupAppListActivity"},
            {"com.vivo.permissionmanager", "com.vivo.permissionmanager.activity.BgStartUpManagerActivity"},
            {"com.huawei.systemmanager", "com.huawei.systemmanager.startupmgr.ui.StartupNormalAppListActivity"},
            {"com.samsung.android.lool", "com.samsung.android.sm.battery.ui.BatteryActivity"},
        };
        for (String[] p : pantallas) {
            try {
                Intent i = new Intent();
                i.setComponent(new ComponentName(p[0], p[1]));
                i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                getContext().startActivity(i);
                JSObject r = new JSObject(); r.put("abrio", p[0]); call.resolve(r);
                return;
            } catch (Exception ignored) { }
        }
        abrirDetalles();
        JSObject r = new JSObject(); r.put("abrio", "detalles"); call.resolve(r);
    }

    private void abrirDetalles() {
        try {
            Intent i = new Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS);
            i.setData(Uri.parse("package:" + getContext().getPackageName()));
            i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            getContext().startActivity(i);
        } catch (Exception ignored) { }
    }
}
''')
ma = os.path.join(pkg, 'MainActivity.java')
mas = open(ma, encoding='utf-8').read()
if 'BateriaPlugin' not in mas:
    mas = '''package com.alexis.viverodomovil;

import android.os.Bundle;
import com.getcapacitor.BridgeActivity;

public class MainActivity extends BridgeActivity {
    @Override
    public void onCreate(Bundle savedInstanceState) {
        registerPlugin(BateriaPlugin.class);   // plugin propio (antes de super.onCreate)
        super.onCreate(savedInstanceState);
    }
}
'''
    open(ma, 'w', encoding='utf-8').write(mas)
print('android listo: firma, runner, permisos y plugin Bateria')
