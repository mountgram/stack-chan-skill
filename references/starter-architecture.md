# Starter Architecture

Read this when you need to decide what belongs in the firmware, the starter skill, or a downstream custom repo.

## Target Shape

```text
StackChan hardware
  display / touch / servos / LEDs / mic / speaker / camera
        |
        | Wi-Fi WebSocket + HTTP audio/image endpoints
        v
Local brain server
  device bridge + voice pipeline + agent tools + debug UI
        |
        | model/STT/TTS APIs
        v
AI and voice providers
```

## Repository Split

| Layer | Generic starter repo | Downstream custom repo |
|---|---|---|
| StackChan hardware facts | yes | no, unless local notes |
| ESP-IDF install flow | yes | no, except local overrides |
| Upstream StackChan vendor flow | yes | no, except pinned commit notes |
| `app_remote_agent` firmware | yes | no duplicate firmware copy |
| Device protocol | yes | may extend locally |
| Bun brain architecture | yes | implementation lives here |
| Personality/system prompt | no | yes |
| Provider/API choices | examples only | yes |
| Tokens, IPs, Wi-Fi | no | yes, in env/local config |

## Firmware Responsibilities

- Connect or reuse Wi-Fi through upstream StackChan facilities.
- Open `/stacky/device` WebSocket with auth token in URL or header.
- Send `hello`, telemetry, touch events, acknowledgements, errors, audio frames, and camera frames.
- Receive commands for screen, face, look, LEDs, speech playback, mic streaming, camera capture, stop, home, and ping.
- Clamp and rate-limit motion locally.
- Keep UI responsive while WebSocket/audio/camera operations run.
- Reconnect without blocking the UI loop.

## Brain Server Responsibilities

- Authenticate the device WebSocket.
- Maintain connected device state and last telemetry.
- Expose typed command helpers for face, look, LEDs, screen, speak, stop, home, mic, and camera.
- Convert user input into agent calls and tool calls.
- Run STT/TTS providers and serve generated audio at a LAN-reachable URL.
- Keep personality, memory, debug UI, and provider choices out of reusable firmware.

## Minimal Blank Repo Layout

```text
vendor/
  esp-idf/                 # ignored skill-local ESP-IDF install
  StackChan/               # ignored upstream m5stack/StackChan checkout
src/
  index.ts                 # Bun.serve entrypoint
  config.ts                # env parsing
  server/routes.ts         # HTTP + WebSocket routes
  device/protocol.ts       # command/event types
  device/commands.ts       # typed command helpers
  device/registry.ts       # connected device state
  device/safety.ts         # clamp helpers
  agent/                   # downstream agent implementation
  voice/                   # downstream STT/TTS implementation
.env.example
README.md
```
