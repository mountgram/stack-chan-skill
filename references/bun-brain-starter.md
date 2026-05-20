# Bun Brain Starter

Read this when creating the local server-side brain for StackChan.

## Package Shape

Use Bun with TypeScript:

```json
{
  "type": "module",
  "module": "src/index.ts",
  "scripts": {
    "dev": "bun --hot src/index.ts",
    "start": "bun src/index.ts",
    "test": "bun test",
    "typecheck": "bunx tsc --noEmit"
  }
}
```

## Suggested Source Layout

```text
src/
  index.ts
  config.ts
  server/
    routes.ts
    debug-page.ts
  device/
    protocol.ts
    registry.ts
    safety.ts
    commands.ts
  agent/
    stacky-agent.ts
    tools.ts
    prompt.ts
  voice/
    stt.ts
    tts.ts
```

## Environment Variables

```dotenv
STACKY_SERVER_HOST=0.0.0.0
STACKY_SERVER_PORT=6001
STACKY_DEVICE_TOKEN=dev-token-change-me
STACKY_PUBLIC_BASE_URL=http://LAN_HOST:6001
AI_PROVIDER=mock
AI_MODEL=
DEEPGRAM_API_KEY=
```

`STACKY_PUBLIC_BASE_URL` must be reachable by StackChan over the LAN. Do not use `localhost` for URLs the device must fetch.

## Required Routes

| Route | Purpose |
|---|---|
| `GET /` | Browser debug UI. |
| `GET /health` | Server and device status without secrets. |
| `GET /audio/:id` | Serve generated PCM audio to firmware. |
| `POST /api/prompt` | Text prompt into the agent. |
| `POST /api/command` | Manual StackChan command for debugging. |
| `WS /stacky/device` | Firmware WebSocket. |
| `WS /stacky/debug` | Browser debug event stream. |

## Device Layer Requirements

- Parse incoming JSON and reject invalid message shapes.
- Store one active device connection at first; add multi-device routing only when needed.
- Keep last telemetry and connection timestamps.
- Broadcast command, ack, error, telemetry, and camera events to debug clients.
- Provide typed helpers: `screen`, `face`, `look`, `led`, `speak`, `startAudio`, `stopAudio`, `captureImage`, `stop`, `home`.
- Clamp yaw, pitch, and speed in code before sending commands.

## Agent Tool Boundary

Expose device actions as model tools instead of asking the model to emit raw JSON:

- `setFace(emotion)`
- `moveHead(yaw?, pitch?, speed?)`
- `setLed(color, pattern?)`
- `getBattery()`
- `lookThroughCamera(question?, enhance?)`

Keep personality in the downstream repo's prompt. The starter should only define physical capabilities, safe limits, and protocol behavior.
