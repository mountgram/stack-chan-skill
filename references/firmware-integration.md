# Firmware Integration

Read this when installing `app_remote_agent` into a vendored StackChan firmware tree.

## Source Asset

Reusable firmware lives in this skill at:

```text
assets/app_remote_agent/
```

Downstream custom repos should not keep a separate generic firmware copy. Symlink this asset into this skill repo's vendored StackChan tree during development.

## Target Vendor Path

The StackChan firmware app path inside this skill repo is:

```text
vendor/StackChan/firmware/main/apps/app_remote_agent/
```

## Install Files

Create the target directory and copy or link:

```text
app_remote_agent.cpp
app_remote_agent.h
```

From the `stack-chan-skill` skill root, run:

```bash
./assets/app_remote_agent/link-into-stackchan.sh
```

The helper expects `assets/app_remote_agent/` and `vendor/StackChan/firmware/main/apps/` to share the same skill root.

## Register The App

In `vendor/StackChan/firmware/main/apps/apps.h`, add:

```cpp
#include "app_remote_agent/app_remote_agent.h"
```

In `vendor/StackChan/firmware/main/main.cpp`, install the app with the other Mooncake apps:

```cpp
GetMooncake().installApp(std::make_unique<AppRemoteAgent>());
```

Keep `AppLauncher` installed first. A safe placement is after `AppAvatar()` and before setup/utility apps.

## Configure WebSocket URL

The firmware supports a compile-time `STACKY_WS_URL` define. Prefer passing it through the firmware build environment instead of editing private IPs into reusable source:

```bash
STACKY_WS_URL='ws://LAN_HOST:6001/stacky/device?token=dev-token-change-me' idf.py build
```

If upstream CMake does not forward this environment variable, add this to `vendor/StackChan/firmware/main/CMakeLists.txt` near the component compile definitions:

```cmake
if(DEFINED ENV{STACKY_WS_URL})
    target_compile_definitions(${COMPONENT_LIB} PRIVATE "STACKY_WS_URL=\"$ENV{STACKY_WS_URL}\"")
endif()
```

Do not commit a private LAN IP or token to reusable files.

## App Behavior

`AppRemoteAgent` should:

- show `REMOTE.AGENT` in the launcher
- connect to the brain WebSocket
- send `hello`, telemetry, tap events, audio frames, and camera frames
- receive screen, face, look, led, speak, startAudio, stopAudio, captureImage, stop, home, and ping commands
- clamp pitch to `5..85` and yaw to `-128..128`
- rate-limit motion commands
- play server-generated PCM audio from a URL
- keep UI responsive and reconnect periodically
