# Full-Stack Voice Agent

Read this when creating a complete StackChan brain server with voice input, LLM reasoning, device tools, and spoken output.

This reference is a reconstruction recipe. Do not assume the target repo already exists. Create the server, protocol, Deepgram voice modules, and agent tool boundary from scratch.

## Target Behavior

- StackChan firmware hosts `WS /stacky/device`; the brain connects out to it.
- The server receives device JSON events and binary mic/camera frames.
- A tap starts a voice conversation.
- Device PCM audio streams to Deepgram live STT.
- End-of-turn transcript enters a Vercel AI SDK tool-loop agent.
- Agent tools send safe physical commands to StackChan.
- Agent text streams into Deepgram TTS.
- The device fetches streamed 24 kHz linear16 PCM from `GET /audio/:id` and plays it.
- `speechDone` returns the system to listening or standby.
- A debug page or debug WebSocket can inspect state, commands, telemetry, transcripts, and camera frames.

## Package

Use Bun and TypeScript.

```json
{
  "type": "module",
  "module": "src/index.ts",
  "scripts": {
    "dev": "bun --hot src/index.ts",
    "start": "bun src/index.ts",
    "test": "bun test",
    "typecheck": "bunx tsc --noEmit"
  },
  "dependencies": {
    "@deepgram/sdk": "latest",
    "@ai-sdk/openai": "latest",
    "ai": "latest",
    "zod": "latest",
    "ws": "latest"
  },
  "devDependencies": {
    "@types/bun": "latest",
    "@types/ws": "latest",
    "typescript": "latest"
  }
}
```

Add other AI SDK providers only when needed, for example `@ai-sdk/anthropic`, `@ai-sdk/google`, or `@openrouter/ai-sdk-provider`.

## Environment

```dotenv
STACKY_SERVER_HOST=0.0.0.0
STACKY_SERVER_PORT=6001
STACKY_DEVICE_WS_URL=ws://STACKCHAN_HOST:6001/stacky/device
STACKY_PUBLIC_BASE_URL=http://LAN_HOST:6001

AI_PROVIDER=openai
AI_MODEL=gpt-5-mini
OPENAI_API_KEY=

DEEPGRAM_API_KEY=
DEEPGRAM_STT_MODEL=nova-3
DEEPGRAM_TTS_MODEL=aura-2-pandora-en
```

`STACKY_DEVICE_WS_URL` points at the WebSocket server hosted by StackChan. The brain sends full URLs in device commands; `STACKY_PUBLIC_BASE_URL` is the base it uses when generating those URLs for local audio endpoints. Do not use `localhost` in URLs the device must fetch.

Minimal `config.ts`:

```ts
const numberFromEnv = (name: string, fallback: number) => {
  const raw = Bun.env[name];
  if (!raw) return fallback;
  const parsed = Number(raw);
  return Number.isFinite(parsed) ? parsed : fallback;
};

export const config = {
  host: Bun.env.STACKY_SERVER_HOST ?? "0.0.0.0",
  port: numberFromEnv("STACKY_SERVER_PORT", 6001),
  publicBaseUrl: Bun.env.STACKY_PUBLIC_BASE_URL ?? `http://localhost:${numberFromEnv("STACKY_SERVER_PORT", 6001)}`,
  deviceWsUrl: Bun.env.STACKY_DEVICE_WS_URL,
  aiProvider: Bun.env.AI_PROVIDER ?? "openai",
  aiModel: Bun.env.AI_MODEL || undefined,
  deepgramApiKey: Bun.env.DEEPGRAM_API_KEY,
  deepgramSttModel: Bun.env.DEEPGRAM_STT_MODEL ?? "nova-3",
  deepgramTtsModel: Bun.env.DEEPGRAM_TTS_MODEL ?? "aura-2-pandora-en",
};
```

## Source Layout

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
    prompt.ts
    tools.ts
    stacky-agent.ts
  voice/
    deepgram-live.ts
    deepgram-streaming-tts.ts
    deepgram-stt.ts
    deepgram-tts.ts
```

Build in this order:

1. Define config and protocol types.
2. Implement device registry and command helpers.
3. Implement Deepgram live STT and streaming TTS modules.
4. Implement AI SDK tools and the agent text stream.
5. Implement server routes, WebSocket handling, and the voice turn coordinator.
6. Add a browser debug page last.

## Protocol Types

Mirror `references/device-protocol.md`, then add the optional commands the firmware may support.

```ts
export type DeviceMode = "offline" | "connecting" | "connected" | "standby" | "listening" | "thinking" | "speaking" | "error";
export type FaceEmotion = "none" | "neutral" | "happy" | "angry" | "sad" | "doubt" | "sleepy";

export type AvatarFeature = { x?: number; y?: number; rotation?: number; weight?: number; size?: number };
export type DecoratorName = "heart" | "angry" | "sweat" | "shy" | "dizzy";

export type DeviceMessage =
  | { type: "hello"; id?: string; version?: number; capabilities?: string[] }
  | { type: "telemetry"; battery?: number; charging?: boolean; wifiRssi?: number; pose?: { yaw?: number; pitch?: number }; volume?: number }
  | { type: "event"; event: "tap" | "wakeWord" | "speechDone" | string; at?: number; wakeWord?: string; phrase?: string; modelId?: string; score?: number }
  | { type: "ack"; requestId?: string; ok?: boolean }
  | { type: "error"; requestId?: string; message: string }
  | { type: string; [key: string]: unknown };

export type DeviceCommand =
  | { type: "screen"; requestId: string; mode?: DeviceMode; text: string }
  | { type: "face"; requestId: string; emotion: FaceEmotion }
  | { type: "look"; requestId: string; yaw?: number; pitch?: number; speed?: number }
  | { type: "led"; requestId: string; color: string; pattern?: "solid" | "pulse" | "off" }
  | { type: "speak"; requestId: string; text: string; audioUrl?: string }
  | { type: "startAudio"; requestId: string }
  | { type: "stopAudio"; requestId: string }
  | { type: "standby"; requestId: string; text?: string; wakeWord?: { enabled: boolean; phrase?: string; modelId?: string; modelUrl?: string } }
  | { type: "captureImage"; requestId: string; enhance?: boolean }
  | { type: "volume"; requestId: string; volume: number }
  | { type: "stop"; requestId: string; target?: "all" | "speech" | "motion" }
  | { type: "home"; requestId: string }
  | { type: "ping"; requestId: string; at: number }
  | { type: "avatarJson"; requestId: string; leftEye?: AvatarFeature; rightEye?: AvatarFeature; mouth?: AvatarFeature }
  | { type: "decorator"; requestId: string; action: "add" | "clear"; name?: DecoratorName; durationMs?: number; animationIntervalMs?: number };
```

Command rules:

- Every command gets a `requestId`.
- Store pending camera captures by `requestId`.
- Treat all incoming JSON as untrusted.
- Clamp servo and volume values before sending commands.

## Device Registry

Create one active device connection first. Multi-device routing is unnecessary until explicitly required.

Registry responsibilities:

- Attach and detach the active device WebSocket.
- Attach and detach debug WebSockets.
- Store `connected`, `connectedAt`, `lastSeenAt`, `capabilities`, `volume`, and latest telemetry.
- Broadcast device messages, commands, camera images, connection changes, and errors to debug clients.
- Provide `send(command)` that serializes the command to the active device or throws if disconnected.
- Provide `waitForImage(requestId, timeoutMs)` for camera tools.

Camera binary handling:

```ts
if (type === 0x32) {
  const metadataBytes = bytes.slice(5, 5 + length);
  const metadata = JSON.parse(new TextDecoder().decode(metadataBytes)) as {
    requestId?: string;
    mediaType?: string;
    width?: number;
    height?: number;
  };
  const jpeg = bytes.slice(5 + length);
  registry.handleCameraImage({
    requestId: metadata.requestId ?? "",
    mediaType: metadata.mediaType ?? "image/jpeg",
    width: metadata.width,
    height: metadata.height,
    data: jpeg,
  });
}
```

## Command Helpers

Expose typed helpers rather than sending raw JSON from application code.

```ts
export function screen(text: string, mode: DeviceMode = "connected") {
  return registry.send({ type: "screen", requestId: commandId(), mode, text: text.slice(0, 180) });
}

export function look(yaw?: number, pitch?: number, speed?: number) {
  return registry.send({
    type: "look",
    requestId: commandId(),
    yaw: yaw === undefined ? undefined : clampYaw(yaw),
    pitch: pitch === undefined ? undefined : clampPitch(pitch),
    speed: clampSpeed(speed),
  });
}

export function speak(text: string, audioUrl?: string) {
  return registry.send({ type: "speak", requestId: commandId(), text: text.slice(0, 500), audioUrl });
}
```

Recommended helpers: `screen`, `face`, `look`, `led`, `speak`, `startAudio`, `stopAudio`, `captureImage`, `volume`, `stop`, `home`, `avatarJson`, `decorator`, and `sendManualCommand`.

## Deepgram Live STT

Use Deepgram live STT for device mic streaming. StackChan sends 16-bit little-endian mono PCM chunks as binary packet type `0x31`.

The `@deepgram/sdk` WebSocket client needs a WebSocket implementation in Bun. Install `ws` and assign it before creating the connection.

```ts
import { DeepgramClient } from "@deepgram/sdk";
import { WebSocket as NodeWebSocket } from "ws";

(globalThis as unknown as { WebSocket: unknown }).WebSocket = NodeWebSocket;

const client = new DeepgramClient({ apiKey: config.deepgramApiKey });
const connection = await client.listen.v2.connect({
  model: "flux-general-en",
  encoding: "linear16",
  sample_rate: 24000,
  Authorization: `Token ${config.deepgramApiKey}`,
});

connection.on("message", (message) => {
  if (message.type === "TurnInfo") {
    onTurn(message.event ?? "Update", message.transcript ?? "");
  }
});

connection.connect();
await connection.waitForOpen();
```

Wrap this in a `DeepgramLiveSession` class with:

- `start()` to connect and wait for open.
- `sendAudio(audio: ArrayBufferView)` to call `connection.sendMedia(audio)` only when open.
- `closeStream()` to send `{ type: "CloseStream" }` if needed.
- `close()` to clear timers and close the socket.

Turn handling:

- Save non-empty transcript updates in `pendingTranscript`.
- On `EagerEndOfTurn` or `EndOfTurn`, call `processTurn()`.
- Ignore empty final turns.
- Broadcast STT events to the debug stream.

## Deepgram Streaming TTS

Use Deepgram Speak WebSocket for low-latency response audio. Request `linear16` at `24000` Hz so the firmware can play raw PCM.

```ts
const connection = await client.speak.v1.connect({
  model: config.deepgramTtsModel,
  encoding: "linear16",
  sample_rate: "24000",
  Authorization: `Token ${config.deepgramApiKey}`,
});

connection.on("message", (message) => {
  if (message instanceof ArrayBuffer) handleAudio(Buffer.from(message));
  else if (message instanceof Buffer) handleAudio(message);
  else if (typeof message === "object" && message && (message as { type?: string }).type === "Flushed") resolveFlush();
});

connection.connect();
await connection.waitForOpen();
```

Implement two paths:

- `speak(text)` buffers all returned audio, writes `.stacky-audio/tts-*.pcm`, and returns `/audio/:id`.
- `speakStream(textChunks)` returns `{ id, url, stream, started, text, done }` immediately and enqueues Deepgram audio chunks into a `ReadableStream<Uint8Array>`.

The streaming path is preferred for conversation:

```ts
const speech = ttsSession.speakStream(streamAgentText(messages));
liveAudioStreams.set(speech.id, speech.stream);

speech.started.then(() => {
  device.screen("Speaking...", "speaking");
  device.speak("", speech.url);
});
```

Important streaming details:

- Enqueue a short initial silence buffer so `GET /audio/:id` can start before the first TTS audio arrives.
- Keep a small silence keepalive until first audio, then clear it.
- Send each agent text chunk with `connection.sendText({ type: "Speak", text: chunk })`.
- After the agent stream ends, call `connection.sendFlush({ type: "Flush" })`.
- Resolve `done` only after Deepgram sends `Flushed` and the audio stream closes.
- On error or timeout, error the stream and reset active turn state.

## One-Shot STT And TTS

Keep one-shot endpoints for browser testing and fallback.

STT:

```ts
await fetch(`https://api.deepgram.com/v1/listen?model=${model}&smart_format=true`, {
  method: "POST",
  headers: { Authorization: `Token ${apiKey}`, "Content-Type": audio.type || "application/octet-stream" },
  body: audio,
});
```

TTS:

```ts
await fetch(`https://api.deepgram.com/v1/speak?model=${model}&encoding=linear16&sample_rate=24000`, {
  method: "POST",
  headers: { Authorization: `Token ${apiKey}`, "Content-Type": "application/json", Accept: "application/octet-stream" },
  body: JSON.stringify({ text }),
});
```

## Agent And Tools

Use the Vercel AI SDK. The model should never emit device protocol JSON directly. Expose physical abilities as tools.

Minimum tools:

- `setFace(emotion)` calls `device.face`.
- `moveHead(yaw?, pitch?, speed?)` calls `device.look`.
- `setLed(color)` calls `device.led`.
- `getBattery()` returns latest telemetry.
- `lookThroughCamera(question?, enhance?)` sends `captureImage`, waits for `waitForImage`, and returns image data to the model.
- `setDecorator(action, name?, durationMs?)` calls `device.decorator` if firmware supports decorators.

Example tool:

```ts
import { tool } from "ai";
import { z } from "zod";

export const lookTool = tool({
  description: "Move StackChan's head. Yaw is -128..128, pitch is 5..85, speed is 0.1..1.",
  inputSchema: z.object({
    yaw: z.number().optional(),
    pitch: z.number().optional(),
    speed: z.number().optional(),
  }),
  execute: async ({ yaw, pitch, speed }) => device.look(yaw, pitch, speed),
});
```

Agent shape:

```ts
const agent = new ToolLoopAgent({
  model,
  instructions: systemPrompt,
  stopWhen: stepCountIs(5),
  maxOutputTokens: 256,
  maxRetries: 1,
  tools: {
    moveHead: lookTool,
    setFace: faceTool,
    setLed: ledTool,
    getBattery: getBatteryTool,
    lookThroughCamera: cameraTool,
  },
});

export async function* streamAgentText(input: string | ModelMessage[]) {
  const result = await agent.stream(typeof input === "string" ? { prompt: input } : { messages: input });
  for await (const chunk of result.textStream) {
    const spoken = chunk.replace(/[*_`]/g, "").replace(/\s+/g, " ");
    if (spoken) yield spoken;
  }
}
```

Add a fallback if an agent performs only tool calls and produces no text: retry once with a text-only agent or speak a short fallback.

## Voice Turn Coordinator

Keep this state in the server module or a small `voice/session.ts` module.

```ts
let sttSession: DeepgramLiveSession | undefined;
let ttsSession: DeepgramStreamingTts | undefined;
let isConversationActive = false;
let isTurnInProgress = false;
let pendingTranscript = "";
let activeSpeechId: string | undefined;
let activeSpeechFinished = false;
let earlySpeechDone = false;
const conversationMessages: ModelMessage[] = [];
const liveAudioStreams = new Map<string, ReadableStream<Uint8Array>>();
```

Conversation start:

1. Require a connected device.
2. Set `isConversationActive = true`.
3. Clear `pendingTranscript` and reset active speech state.
4. Start STT and TTS sessions.
5. Send `screen("Listening...", "listening")`, LED feedback, and `startAudio()`.
6. Start a listen timeout so the robot returns to standby if the user stops talking.

Standby:

1. Send `standby` instead of a plain `screen` update.
2. If the device advertises `wakeWord`, include the server-selected `wakeWord` config.
3. If wake-word support is absent, omit `wakeWord` and preserve tap-to-talk.
4. On `event: "wakeWord"`, call the same conversation start path used by tap-to-talk.

Processing a turn:

1. Guard against concurrent turns with `isTurnInProgress`.
2. Clear listen timeout.
3. Send `stopAudio()` so the mic does not hear the robot's speaker.
4. Show `screen("Thinking...", "thinking")`.
5. Build messages from recent conversation plus the latest transcript.
6. Ensure TTS is connected.
7. Start `ttsSession.speakStream(streamAgentText(messages))`.
8. Store the stream in `liveAudioStreams` by speech id.
9. When speech starts, send `speak("", speech.url)` to the device.
10. When agent text completes, append user and assistant turns to short history.
11. When TTS flush completes, mark `activeSpeechFinished = true`.
12. If the device already sent `speechDone`, finish the turn then.

Finishing speech:

1. On device event `speechDone`, do not immediately reopen the mic if TTS is still flushing.
2. If TTS is done, set `isTurnInProgress = false`.
3. If conversation is active, show listening state and send `startAudio()`.
4. If not active, enter standby.

Tap handling:

- Single tap starts conversation from standby.
- While active, ignore single taps unless you want push-to-interrupt behavior.
- Double tap within about `450 ms` stops everything and enters standby.

Binary audio handling:

```ts
if (type === 0x31) {
  const audioBytes = bytes.slice(5, 5 + length);
  if (isConversationActive && !isTurnInProgress && hasSpeechActivity(audioBytes)) scheduleListenTimeout();
  sttSession?.sendAudio(audioBytes);
}
```

Use a simple RMS gate to avoid extending listen timeout for silence.

## Server Routes

Required routes:

| Route | Purpose |
|---|---|
| `GET /` | Browser debug UI. |
| `GET /health` | Config and device state without secrets. |
| `GET /audio/:id` | Serves live TTS streams or saved PCM files. |
| `POST /api/prompt` | Text prompt into agent for testing. |
| `POST /api/audio-prompt` | Multipart audio upload through one-shot STT and agent. |
| `POST /api/voice/start` | Start tap-to-talk conversation from browser. |
| `POST /api/voice/stop` | Stop conversation and enter standby. |
| `POST /api/command` | Manual device command for debugging. |
| `WS /stacky/debug` | Browser debug event stream. |

`GET /audio/:id` should first check `liveAudioStreams`, then `.stacky-audio` files:

```ts
if (liveAudioStreams.has(id)) {
  const stream = liveAudioStreams.get(id)!;
  liveAudioStreams.delete(id);
  return new Response(stream, {
    headers: { "Content-Type": "audio/L16; rate=24000; channels=1", "Cache-Control": "no-store" },
  });
}
```

Device WebSocket client:

```ts
const ws = new WebSocket(config.deviceWsUrl);
ws.binaryType = "arraybuffer";
ws.addEventListener("open", () => registry.attachDevice(ws));
ws.addEventListener("message", (event) => {
  if (typeof event.data === "string") handleDeviceJson(event.data);
  else handleDeviceBinary(new Uint8Array(event.data as ArrayBufferLike));
});
```

On device WebSocket open:

- Attach registry.
- Send current volume if available.
- Show standby screen.

On message:

- If binary, dispatch by packet type.
- If JSON, parse as `DeviceMessage`, update registry, handle `tap`, `wakeWord`, and `speechDone` events.

On close:

- Detach registry.
- Close STT and TTS sessions.
- Clear active turn state.

## Debug UI

Keep debug UI optional but useful. It should connect to `/stacky/debug` and render:

- device connected state
- latest telemetry
- recent protocol log
- last camera image
- transcript/STT events
- buttons for `voice/start`, `voice/stop`, `home`, `stop`, `captureImage`, `volume`, `face`, `look`, and `led`

Do not expose secrets in debug HTML or `/health`.

## Safety Rules

- Clamp yaw to `-128..128`.
- Clamp pitch to `5..85`.
- Clamp speed to `0.1..1`.
- Clamp volume to `0..100`.
- Truncate screen text and spoken text before sending to firmware.
- Stop mic capture before speaker playback to avoid feedback loops.
- Close Deepgram sessions when the device disconnects.
- Use request timeouts around agent/TTS turns, usually about `60 seconds`.
- Keep conversation history short, for example last 12 model messages.
- Never log API keys, device tokens, or private LAN-specific secrets.

## Validation Checklist

- `bun install` succeeds.
- `bunx tsc --noEmit` passes.
- `bun test` passes for safety clamps if tests exist.
- `GET /health` returns config booleans and disconnected device state.
- Brain connects to `STACKY_DEVICE_WS_URL`; device sends `hello` after the WebSocket opens.
- `/stacky/debug` receives connection and telemetry events.
- Manual `screen`, `face`, `look`, `led`, `home`, and `stop` commands work.
- Tap starts live STT and sends `startAudio`.
- Device audio packets reach Deepgram and produce transcript events.
- End of turn calls the agent and at least one physical tool can execute.
- Deepgram TTS stream is available from `/audio/:id` as `audio/L16; rate=24000; channels=1`.
- Device sends `speechDone`, then server returns to listening or standby correctly.
