# StackChan Overview

Read this when you need to understand what StackChan is before designing firmware, protocol, or server behavior.

## What StackChan Is

StackChan is an M5Stack CoreS3-based desktop robot: a small expressive head/body with display, servos, LEDs, camera, microphones, speaker, touch input, Wi-Fi, and battery.

For a remote-agent starter, treat StackChan as a networked body terminal:

- StackChan firmware owns hardware access, local UI, safe motion, audio/camera capture, and reconnect behavior.
- The server owns AI orchestration, speech/text generation, memory, tool calls, logs, and personality.
- The protocol between them should stay small and explicit.

## Useful Hardware Capabilities

| Capability | Starter use |
|---|---|
| 320x240 touch display | Show status, short subtitles, errors, and tap target. |
| Two servos | Express yaw and pitch with hard safety clamps. |
| 12 RGB LEDs | Show listening, thinking, speaking, error, or emotion states. |
| Dual microphones | Stream PCM audio to server-side STT. |
| Speaker | Play server-generated PCM audio from an HTTP URL/stream. |
| Camera | Capture JPEG images for vision tool calls. |
| Wi-Fi | Persistent WebSocket to the brain server. |
| Battery/charging | Telemetry and debug UI state. |
| Touch panel/display tap | Wake, start/stop listening, interruption events. |

## Safety Rules

- Do not force movable parts by hand while motors are powered or under control.
- Clamp vertical pitch to a safe range; this starter uses `5..85` degrees.
- Rate-limit servo movement; do not allow raw model output to write servo values directly.
- Prefer the base USB-C port when flashing or debugging to reduce accidental movement.
- Keep firmware as the final hardware safety boundary even if the server also clamps commands.

## Upstream Repository

Use `https://github.com/m5stack/StackChan` as the upstream source for firmware, mobile app, remote controller, and server references.

That upstream repository contains the ESP-IDF firmware project in:

```text
firmware/
```

When vendored into this skill repo, the firmware project root becomes:

```text
vendor/StackChan/firmware
```

Vendor the upstream repo into this skill's ignored `vendor/` directory instead of copying the full upstream source into tracked files.
