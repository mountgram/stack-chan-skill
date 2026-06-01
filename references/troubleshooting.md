# Troubleshooting

Read this when StackChan setup, firmware, or server integration fails.

| Symptom | Check | Fix |
|---|---|---|
| `idf.py: command not found` | ESP-IDF environment is not active in this shell. | Run `. ./vendor/esp-idf/export.sh` from the `stack-chan-skill` root, then retry. |
| `vendor/esp-idf/export.sh` missing | ESP-IDF was not installed in the skill vendor path. | Follow `references/esp-idf-install.md`. |
| `idf.py` says no project | Command was run from app repo root or wrong directory. | Run `idf.py` from this skill repo's `vendor/StackChan/firmware`. |
| `fetch_repos.py` missing | `vendor/StackChan` is absent or not the upstream repo root. | Clone `https://github.com/m5stack/StackChan` into `vendor/StackChan`. |
| `app_remote_agent/app_remote_agent.h` not found | App files are not linked/copied into the vendor app directory. | Install `assets/app_remote_agent/` into `vendor/StackChan/firmware/main/apps/app_remote_agent/`. |
| `AppRemoteAgent` unknown in `main.cpp` | `apps.h` does not include the app header. | Add `#include "app_remote_agent/app_remote_agent.h"` to `apps.h`. |
| App builds but is missing from launcher | `main.cpp` does not install the app. | Add `GetMooncake().installApp(std::make_unique<AppRemoteAgent>());`. |
| Firmware connects to wrong server | `STACKY_WS_URL` was hard-coded or not passed to build. | Rebuild with `STACKY_WS_URL='ws://LAN_HOST:PORT/stacky/device?token=TOKEN' idf.py build`. |
| Firmware cannot reach audio URLs | Server uses `localhost` or unreachable host in `STACKY_PUBLIC_BASE_URL`. | Set `STACKY_PUBLIC_BASE_URL` to a LAN-reachable URL. |
| WebSocket unauthorized | Token mismatch. | Match firmware URL token and server `STACKY_DEVICE_TOKEN`. |
| Motion is jerky or unsafe | Server sends frequent/raw model values. | Clamp in server, rely on firmware clamps, and rate-limit commands. |
| Flash picks wrong serial port | Multiple USB serial devices are connected. | Inspect ports and pass `idf.py -p PORT flash`. |
| Linux flash permission denied | User lacks serial group permission. | Add user to `dialout` or distro-equivalent group, then re-login. |
| Camera images are too dark | Low-light scene or sensor exposure behavior. | Try `captureImage` with `enhance: true`; consider firmware sensor controls if supported. |
| Device stays on `Streaming mic...` after `speechDone` and the next `startAudio` | Server receives `ack` for `startAudio`, then no binary PCM, no telemetry, and no later command acks. This points to firmware-side audio capture or codec transition wedging after playback, not Deepgram STT. | Use the current `app_remote_agent` asset so `startAudio` ack is delayed until a PCM frame is queued, inspect logs around wake-word disarm, `EnableInput(true)`, first `InputData(...)`, first PCM frame queued/sent, `EnableOutput(false)`, and the audio start timeout. If it still wedges after `first InputData attempt`, suspect a blocking codec/HAL call. |
| `SystemInfo` reports very low minimum SRAM | Camera, render scene creation, playback, wake-word inference, or per-frame audio allocations are exhausting internal SRAM margin. | Use the current `app_remote_agent` asset: it rejects camera capture during audio, adds internal-SRAM guards for camera/render/speech, logs heap around expensive operations, and reuses the mic input buffer instead of allocating it every frame. |

## Recovery Principles

- Do not delete `vendor/StackChan` changes to fix a build unless the user explicitly approves.
- Do not overwrite `sdkconfig` casually; `idf.py set-target` can reset configuration.
- Do not flash when the serial port is ambiguous.
- Report the exact command output for build/flash failures.

## Wake-Word Audio Restart Wedge

The wake-word flow exercises the codec transition more often than the old tap-only path:

```text
wake/tap -> startAudio -> stopAudio -> speak/playback -> speechDone -> startAudio
```

If the second `startAudio` after playback leaves the display at `Streaming mic...`, collect these timestamps from firmware logs before changing server code:

1. received `startAudio`
2. wake-word disarm start/end
3. `_audio_streaming = true`
4. capture loop sees streaming true
5. `EnableInput(true)` start/end
6. first `InputData(...)` attempt
7. first successful `InputData(...)`
8. first PCM frame queued
9. first PCM frame sent from `sendQueuedAudioFrames`
10. received `stopAudio`
11. `_audio_streaming = false`
12. `EnableInput(false)` start/end
13. playback start/end and `EnableOutput(false)` start/end
14. `speechDone` sent

Expected fixed behavior: firmware does not ack `startAudio` until a frame is queued. On startup failure it sends `error` with `audio capture start timed out`, disables input, and returns to a recoverable status instead of staying permanently stuck.
