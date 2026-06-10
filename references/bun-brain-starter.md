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
STACKY_DEVICE_WS_URL=ws://STACKCHAN_HOST:6001/stacky/device
DEEPGRAM_API_KEY=
```

`STACKY_DEVICE_WS_URL` points at the WebSocket server hosted by StackChan. TTS playback uses binary frames on that WebSocket, so StackChan does not need to fetch HTTP audio URLs.

## Required Routes

| Route | Purpose |
|---|---|
| `GET /` | Browser debug UI. |
| `GET /health` | Server and device status without secrets. |
| `POST /api/prompt` | Text prompt into the agent. |
| `POST /api/command` | Manual StackChan command for debugging. |
| outbound `STACKY_DEVICE_WS_URL` | Device-hosted firmware WebSocket. |
| `WS /stacky/debug` | Browser debug event stream. |

## Remote Brain Through Tailscale

If the brain runs on Hetzner, run `bun run proxy:stacky` on a LAN machine that can reach StackChan and is on the same Tailscale tailnet. Set the Hetzner brain's `STACKY_DEVICE_WS_URL` to `ws://LAN-MACHINE-MAGICDNS:6002/stacky/device`. The proxy forwards text and binary WebSocket frames unchanged to `STACKY_PROXY_TARGET_WS_URL`.

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
