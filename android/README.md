# Darpan Android app

Kotlin client (`com.darpan.remote`) using **OkHttp** signaling and **Stream WebRTC** for P2P data channels (`darpan.preview`, `darpan.control`).

## Build

Requires **JDK 17**, **Android SDK 35**, and `ANDROID_HOME` set (Android Studio installs both).

```powershell
cd android
.\gradlew.bat assembleDebug
```

APK: `app/build/outputs/apk/debug/app-debug.apk`

See `docs/ANDROID_TEST.md` and `docs/ANDROID_SCOPE.md`.
