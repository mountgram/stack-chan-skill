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

## Boot Into Remote Agent

To make StackChan boot directly into `REMOTE.AGENT` while keeping the launcher installed and reachable from the home button, patch the launcher from the `stack-chan-skill` skill root:

```bash
./assets/app_remote_agent/boot-into-remote-agent.sh
```

This patch keeps `AppLauncher` installed first. On boot, the launcher still initializes and handles first-run setup, then opens the app named `REMOTE.AGENT` once. The remote app's home indicator still calls `close()`, which returns to the launcher through Mooncake's normal launcher behavior.

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

## Configure WebSocket Server

The firmware hosts `ws://<stackchan>:6001/stacky/device`. It requires the `esp_http_server` component and `CONFIG_HTTPD_WS_SUPPORT=y`; `scripts/patch-stackchan.sh` applies those build settings.

Do not commit private LAN IPs to reusable files. Put the StackChan URL in the brain server environment as `STACKY_DEVICE_WS_URL`. Firmware does not construct brain URLs; the brain sends full device-reachable URLs in commands.

## App Behavior

`AppRemoteAgent` should:

- show `REMOTE.AGENT` in the launcher
- open automatically after boot while preserving the launcher and home button path back to it
- host the device WebSocket and know when the brain is connected
- send `hello`, telemetry, tap events, audio frames, and camera frames
- receive screen, face, look, led, speak, startAudio, stopAudio, captureImage, stop, home, and ping commands
- receive standby commands; if wake-word support is present, arm the server-selected local detector only in standby
- receive `render.defineScene`, `render.setScene`, `render.reset`, and play `render.animate` keyframes for server-driven avatar rendering
- clamp pitch to `5..85` and yaw to `-128..128`
- rate-limit motion commands
- play server-generated PCM audio from a URL
- keep UI responsive and reconnect periodically

## Wake-Word Integration Point

Keep wake-word detection out of the always-streaming STT path. `startAudio` streams microphone PCM to the server; `standby` stops that stream and may arm a local microWakeWord detector if the server requested one.

Firmware must not advertise `wakeWord` until it can actually run the detector. Once integrated, include `wakeWord` in `hello.capabilities`, provide model metadata in `hello.wakeWord.models`, and emit:

```json
{ "type": "event", "event": "wakeWord", "wakeWord": "Stacky", "modelId": "stacky", "score": 0.98 }
```

Do not hard-code `Stacky` as the only policy. It can be the default model, but the server chooses the active standby phrase/model via the `standby.wakeWord` payload.
