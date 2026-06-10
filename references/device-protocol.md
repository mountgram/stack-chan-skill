# Device Protocol

Read this when implementing firmware or server messages.

## Transport

- Device hosts WebSocket: `ws://STACKCHAN_HOST:6001/stacky/device`.
- The brain server connects out to the device-hosted WebSocket.
- JSON text frames carry commands and events.
- Binary frames carry PCM audio or raw camera image payloads.
- Any URL field sent to firmware, such as `speak.audioUrl` or `standby.wakeWord.modelUrl`, must be a full device-reachable URL. Firmware fetches it as-is and does not resolve relative paths against the WebSocket URL.

## Device To Server JSON

```json
{ "type": "hello", "id": "stacky-abc", "version": 2, "capabilities": ["screen", "face", "look", "led", "telemetry", "tap", "audio", "camera", "volume", "standby", "render"] }
{ "type": "hello", "id": "stacky-abc", "version": 2, "capabilities": ["screen", "face", "look", "led", "telemetry", "tap", "audio", "camera", "volume", "standby", "wakeWord", "render"], "wakeWord": { "version": 1, "models": [{ "id": "stacky", "phrase": "Stacky", "sampleRate": 16000, "cutoff": 0.97, "slidingWindow": 5 }] } }
{ "type": "telemetry", "battery": 82, "charging": true, "wifiRssi": -55, "pose": { "yaw": 0, "pitch": 35 } }
{ "type": "event", "event": "tap", "at": 123456 }
{ "type": "event", "event": "wakeWord", "wakeWord": "Stacky", "modelId": "stacky", "score": 0.98, "at": 123456 }
{ "type": "event", "event": "cameraImage", "requestId": "img-1", "width": 160, "height": 120, "mediaType": "image/bmp", "bytes": 57654 }
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
{ "type": "captureImage", "requestId": "img-1", "enhance": false, "preview": true }
{ "type": "captureImage", "requestId": "img-2", "enhance": true, "preview": true }
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
- `startAudio` means stream microphone PCM to the server for STT. Firmware should acknowledge it only after mic input is enabled and at least one PCM frame has been captured/queued; if startup fails or no frame is produced within about 2 seconds, send `error` for that `requestId` and stop streaming.
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

Packetized binary frames start with:

```text
byte 0: packet type
bytes 1-4: big-endian length N
bytes 5..: payload
```

| Type | Direction | Payload |
|---|---|---|
| `0x31` | device to server | 16-bit little-endian mono PCM audio chunk. |
| raw binary after `cameraImage` event | device to server | Image bytes described by the immediately preceding `cameraImage` JSON event. |
| `0x32` | device to server | Legacy camera packet: JSON metadata of length `N`, followed by image bytes. |

Camera metadata example:

```json
{ "requestId": "img-1", "width": 320, "height": 240, "mediaType": "image/jpeg" }
```

## Camera Capture Semantics

Prefer `preview: true` for debug UI camera buttons and other casual captures. The reusable firmware preview path avoids JPEG encoding to reduce SRAM pressure during voice interactions.

Current preview behavior:

- `captureImage` with `preview: true, enhance: false` returns a small 8-bit grayscale BMP preview, usually `160x120` and about `20278` bytes.
- `captureImage` with `preview: true, enhance: true` returns a low-memory enhanced BMP preview. If the camera source frame is YUYV, this is a 24-bit color BMP, usually `160x120` and about `57654` bytes. If the camera source is GREY, the result is necessarily grayscale.
- `captureImage` with `preview: false` may use the JPEG path. Reserve that for agent vision paths that need full quality and can tolerate higher memory use; it may fail with `low memory for camera capture` while audio, wake-word, rendering, or speech resources are active.

Servers should handle the modern camera protocol by pairing a JSON event like:

```json
{ "type": "event", "event": "cameraImage", "requestId": "img-1", "width": 160, "height": 120, "mediaType": "image/bmp", "bytes": 57654 }
```

with the next binary WebSocket message as the image bytes. Keep legacy `0x32` parsing only for older firmware compatibility.

To avoid interleaving and memory pressure, servers should allow only one active camera request at a time. It is also reasonable to deny debug camera capture in standby if wake-word standby is armed; capture during active voice can work when serialized.

## Wake-Word Standby

Wake-word standby is intentionally server-selected. The firmware exposes capability, available models, and whether dynamic model download is supported. The server decides whether to arm a wake word, which phrase/model to use, or to fall back to tap-only standby.

The default desired phrase is `Stacky`. A custom phrase requires a matching microWakeWord model; changing the text alone does not create a usable detector.

## Audio Startup Semantics

Treat `startAudio` ack as "mic is actually streaming," not merely "command accepted." The robust sequence is:

1. Server sends `{ "type": "startAudio", "requestId": "cmd-123" }`.
2. Firmware disarms wake-word detection and requests capture startup.
3. Capture task enables codec input and attempts `InputData(...)`.
4. Firmware sends `{ "type": "ack", "requestId": "cmd-123", "ok": true }` only after the first PCM frame is captured or queued.
5. If no PCM frame is produced within about 2 seconds, firmware disables input, stops streaming, and sends `{ "type": "error", "requestId": "cmd-123", "message": "audio capture start timed out" }`.

This distinguishes command acceptance from a healthy audio pipeline, especially after `speechDone` when the server immediately starts the next listening turn.
