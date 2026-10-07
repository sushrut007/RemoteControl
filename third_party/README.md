# Third-party native dependencies (Windows desktop)

Qt **6.8.3** is not vendored here — install separately and pass `-DCMAKE_PREFIX_PATH` at configure time.

## libdatachannel (WebRTC)

Prebuilt MSVC **x64** import library + DLL + public headers.

```text
third_party/libdatachannel/
  include/rtc/...
  bin/datachannel.lib
  bin/datachannel.dll
```

CMake uses this path by default (`cmake/LibDataChannel.cmake`). Override with `-DDARPAN_LIBDATACHANNEL_ROOT=...` if needed.

At build time, `datachannel.dll` is copied next to `Darpan.exe`. `windeployqt` still deploys Qt DLLs from your Qt installation.

To refresh binaries after rebuilding libdatachannel elsewhere, copy `include/`, `datachannel.lib`, and `datachannel.dll` into this tree.
