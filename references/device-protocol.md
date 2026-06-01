# Device Protocol

Read this when implementing firmware or server messages.

## Transport

- Device connects to server WebSocket: `/stacky/device`.
- Auth can use `?token=...` or an `x-stacky-token` header.
- JSON text frames carry commands and events.
- Binary frames carry PCM audio or JPEG camera payloads.

## Device To Server JSON

```json
{ "type": "hello", "id": "stacky-abc", "version": 1, "capabilities": ["screen", "face", "look", "led", "telemetry", "tap", "audio", "camera", "standby"] }
{ "type": "hello", "id": "stacky-abc", "version": 1, "capabilities": ["screen", "face", "look", "led", "telemetry", "tap", "audio", "camera", "standby", "wakeWord"], "wakeWord": { "version": 1, "models": [{ "id": "stacky", "phrase": "Stacky", "source": "firmware" }], "dynamicModels": false } }
{ "type": "telemetry", "battery": 82, "charging": true, "wifiRssi": -55, "pose": { "yaw": 0, "pitch": 35 } }
{ "type": "event", "event": "tap", "at": 123456 }
{ "type": "event", "event": "wakeWord", "wakeWord": "Stacky", "modelId": "stacky", "score": 0.98, "at": 123456 }
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
{ "type": "standby", "requestId": "cmd-8", "text": "Standby. Say \"Stacky\".", "wakeWord": { "enabled": true, "phrase": "Stacky", "modelId": "stacky" } }
{ "type": "captureImage", "requestId": "img-1", "enhance": false }
{ "type": "stop", "requestId": "cmd-9", "target": "all" }
{ "type": "home", "requestId": "cmd-10" }
{ "type": "ping", "requestId": "cmd-11", "at": 123456 }
```

## Command Rules

- Every server command gets a `requestId`.
- Firmware sends exactly one `ack` or `error` for each command when practical.
- Firmware should reject malformed JSON with an error, not crash.
- Server clamps values before sending; firmware clamps again before touching hardware.
- Firmware can no-op unsupported commands with `ack` only when that is safer than erroring.
- `startAudio` means stream microphone PCM to the server for STT.
- `standby` means stop full-audio streaming and enter the server-selected idle mode.
- Firmware must advertise `wakeWord` only when it can run a local detector. If `wakeWord` is absent, the server should use tap-only standby.
- A local detector sends `wakeWord` when it fires; the server then starts a normal STT conversation with `startAudio`.

## Enums And Limits

| Field | Values |
|---|---|
| `mode` | `offline`, `connecting`, `connected`, `standby`, `listening`, `thinking`, `speaking`, `error` |
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

## Wake-Word Standby

Wake-word standby is intentionally server-selected. The firmware exposes capability, available models, and whether dynamic model download is supported. The server decides whether to arm a wake word, which phrase/model to use, or to fall back to tap-only standby.

The default desired phrase is `Stacky`. A custom phrase requires a matching microWakeWord model; changing the text alone does not create a usable detector.
