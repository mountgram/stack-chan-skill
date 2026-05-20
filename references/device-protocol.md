# Device Protocol

Read this when implementing firmware or server messages.

## Transport

- Device connects to server WebSocket: `/stacky/device`.
- Auth can use `?token=...` or an `x-stacky-token` header.
- JSON text frames carry commands and events.
- Binary frames carry PCM audio or JPEG camera payloads.

## Device To Server JSON

```json
{ "type": "hello", "id": "stacky-abc", "version": 1, "capabilities": ["screen", "face", "look", "led", "telemetry", "tap", "audio", "camera"] }
{ "type": "telemetry", "battery": 82, "charging": true, "wifiRssi": -55, "pose": { "yaw": 0, "pitch": 35 } }
{ "type": "event", "event": "tap", "at": 123456 }
{ "type": "event", "event": "speechDone" }
{ "type": "ack", "requestId": "cmd-1", "ok": true }
{ "type": "error", "requestId": "cmd-2", "message": "pitch out of range" }
```

## Server To Device JSON

```json
{ "type": "screen", "requestId": "cmd-1", "mode": "thinking", "text": "Thinking..." }
{ "type": "face", "requestId": "cmd-2", "emotion": "happy" }
{ "type": "look", "requestId": "cmd-3", "yaw": 10, "pitch": 35, "speed": 0.5 }
{ "type": "led", "requestId": "cmd-4", "color": "#33cc99", "pattern": "pulse" }
{ "type": "speak", "requestId": "cmd-5", "text": "Hello", "audioUrl": "http://LAN_HOST:6001/audio/id" }
{ "type": "startAudio", "requestId": "cmd-6" }
{ "type": "stopAudio", "requestId": "cmd-7" }
{ "type": "captureImage", "requestId": "img-1", "enhance": false }
{ "type": "stop", "requestId": "cmd-8", "target": "all" }
{ "type": "home", "requestId": "cmd-9" }
{ "type": "ping", "requestId": "cmd-10", "at": 123456 }
```

## Command Rules

- Every server command gets a `requestId`.
- Firmware sends exactly one `ack` or `error` for each command when practical.
- Firmware should reject malformed JSON with an error, not crash.
- Server clamps values before sending; firmware clamps again before touching hardware.
- Firmware can no-op unsupported commands with `ack` only when that is safer than erroring.

## Enums And Limits

| Field | Values |
|---|---|
| `mode` | `offline`, `connecting`, `connected`, `listening`, `thinking`, `speaking`, `error` |
| `emotion` | `neutral`, `happy`, `curious`, `thinking`, `sad`, `surprised`, `asleep`, `dizzy` |
| `pattern` | `solid`, `pulse`, `off` |
| `yaw` | `-128..128` |
| `pitch` | `5..85` |
| `speed` | `0.1..1` |

## Binary Frames

Binary frames start with:

```text
byte 0: packet type
bytes 1-4: big-endian length N
bytes 5..: payload
```

| Type | Direction | Payload |
|---|---|---|
| `0x31` | device to server | 16-bit little-endian mono PCM audio chunk. |
| `0x32` | device to server | JSON metadata of length `N`, followed by JPEG bytes. |

Camera metadata example:

```json
{ "requestId": "img-1", "width": 320, "height": 240, "mediaType": "image/jpeg" }
```
