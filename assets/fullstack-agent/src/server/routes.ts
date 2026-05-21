import { config, healthConfig } from "../config";
import type { ServerWebSocket } from "bun";
import type { ModelMessage } from "ai";
import { streamStackyAgentText } from "../agent/stacky-agent";
import * as device from "../device/commands";
import { parseDeviceMessage } from "../device/protocol";
import { registry, type StackyWsData } from "../device/registry";
import { DeepgramLiveSession, type LiveSttEvent } from "../voice/deepgram-live";
import { DeepgramStreamingTts } from "../voice/deepgram-streaming-tts";
import { debugPage } from "./debug-page";

const json = (body: unknown, status = 200) => Response.json(body, { status });
const TURN_TIMEOUT_MS = 60_000;
const MAX_HISTORY_MESSAGES = 12;
const LISTEN_TIMEOUT_MS = 15_000;
const DOUBLE_TAP_MS = 450;
const SPEECH_ACTIVITY_RMS = 600;

// Persistent voice session state
let sttSession: DeepgramLiveSession | undefined;
let ttsSession: DeepgramStreamingTts | undefined;
let isConversationActive = false;
let isTurnInProgress = false;
let pendingTranscript = "";
let activeSpeechId: string | undefined;
let activeSpeechFinished = false;
let earlySpeechDone = false;
let listenTimeout: Timer | undefined;
let lastTapAt = 0;
let conversationGeneration = 0;
const conversationMessages: ModelMessage[] = [];
const liveAudioStreams = new Map<string, ReadableStream<Uint8Array>>();

async function withTimeout<T>(promise: Promise<T>, ms: number, label: string): Promise<T> {
  let timeout: Timer | undefined;
  try {
    return await Promise.race([
      promise,
      new Promise<never>((_, reject) => {
        timeout = setTimeout(() => reject(new Error(`${label} timed out`)), ms);
      }),
    ]);
  } finally {
    if (timeout) clearTimeout(timeout);
  }
}

function handleBinaryDeviceMessage(bytes: Uint8Array) {
  if (bytes.length < 5) return false;
  const type = bytes[0];
  const length = ((bytes[1] ?? 0) << 24) | ((bytes[2] ?? 0) << 16) | ((bytes[3] ?? 0) << 8) | (bytes[4] ?? 0);

  if (type === 0x31) {
    const audioBytes = bytes.slice(5, 5 + length);
    registry.handleDeviceMessage({ type: "event", event: "audio", bytes: audioBytes.length, streaming: true });
    if (isConversationActive && !isTurnInProgress && hasSpeechActivity(audioBytes)) scheduleListenTimeout();
    sttSession?.sendAudio(audioBytes);
    return true;
  }

  if (type === 0x32) {
    const metaBytes = bytes.slice(5, 5 + length);
    const meta = JSON.parse(new TextDecoder().decode(metaBytes)) as { requestId?: string; mediaType?: string; width?: number; height?: number };
    registry.handleCameraImage({
      requestId: meta.requestId ?? "",
      mediaType: meta.mediaType ?? "image/jpeg",
      width: meta.width,
      height: meta.height,
      data: bytes.slice(5 + length),
    });
    return true;
  }

  return false;
}

function handleSttEvent(event: LiveSttEvent) {
  registry.handleDeviceMessage({ type: "event", event: "stt", stt: event });
  if (event.type === "turn" && event.transcript.trim()) {
    if (isConversationActive && !isTurnInProgress) scheduleListenTimeout();
    pendingTranscript = event.transcript.trim();
    if (event.event === "EagerEndOfTurn" || event.event === "EndOfTurn") {
      processTurn().catch((error) => {
        registry.handleDeviceMessage({
          type: "error",
          message: error instanceof Error ? error.message : String(error),
        });
      });
    }
  }
}

function rememberTurn(userText: string, assistantText: string) {
  conversationMessages.push({ role: "user", content: userText });
  if (assistantText.trim()) {
    conversationMessages.push({ role: "assistant", content: assistantText.trim() });
  }
  conversationMessages.splice(0, Math.max(0, conversationMessages.length - MAX_HISTORY_MESSAGES));
}

function hasSpeechActivity(audio: Uint8Array) {
  if (audio.length < 2) return false;
  let sumSquares = 0;
  let samples = 0;
  for (let i = 0; i + 1 < audio.length; i += 2) {
    const sample = (audio[i] ?? 0) | ((audio[i + 1] ?? 0) << 8);
    const signed = sample > 0x7fff ? sample - 0x10000 : sample;
    sumSquares += signed * signed;
    samples++;
  }
  return Math.sqrt(sumSquares / samples) >= SPEECH_ACTIVITY_RMS;
}

function clearListenTimeout() {
  if (!listenTimeout) return;
  clearTimeout(listenTimeout);
  listenTimeout = undefined;
}

function scheduleListenTimeout() {
  clearListenTimeout();
  listenTimeout = setTimeout(() => {
    if (!isConversationActive || isTurnInProgress) return;
    console.log("[voice] listen timeout, returning to standby");
    enterStandby().catch((error) => {
      registry.handleDeviceMessage({ type: "error", message: error instanceof Error ? error.message : String(error) });
    });
  }, LISTEN_TIMEOUT_MS);
}

function showStandby() {
  if (!registry.hasDevice()) return;
  device.screen("Standby. Tap to talk.", "connected");
  device.face("none");
  device.led("#224466");
}

function closeVoiceSessions() {
  isConversationActive = false;
  conversationGeneration++;
  clearListenTimeout();
  pendingTranscript = "";
  isTurnInProgress = false;
  activeSpeechId = undefined;
  activeSpeechFinished = false;
  earlySpeechDone = false;
  sttSession?.close();
  sttSession = undefined;
  ttsSession?.close();
  ttsSession = undefined;
}

async function ensureStt() {
  if (!sttSession) {
    sttSession = new DeepgramLiveSession(handleSttEvent);
    await sttSession.start();
  }
}

async function ensureTts() {
  if (!ttsSession) {
    ttsSession = new DeepgramStreamingTts();
    await ttsSession.start();
  }
}

async function startConversation() {
  const gen = ++conversationGeneration;
  if (!registry.hasDevice()) return { alreadyActive: isConversationActive, deviceConnected: false };
  if (isConversationActive) {
    if (isTurnInProgress) return { alreadyActive: true, turnInProgress: true };
    pendingTranscript = "";
    device.screen("Waking up...", "thinking");
    device.led("#33cc99");
    await ensureStt();
    if (gen !== conversationGeneration) return;
    await ensureTts();
    if (gen !== conversationGeneration) return;
    device.screen("Listening...", "listening");
    scheduleListenTimeout();
    return { alreadyActive: true, restartedAudio: true, command: device.startAudio() };
  }
  isConversationActive = true;
  pendingTranscript = "";
  activeSpeechId = undefined;
  activeSpeechFinished = false;
  earlySpeechDone = false;
  conversationMessages.length = 0;
  device.screen("Waking up...", "thinking");
  device.led("#33cc99");
  await ensureStt();
  if (gen !== conversationGeneration) return;
  await ensureTts();
  if (gen !== conversationGeneration) return;
  device.screen("Listening...", "listening");
  scheduleListenTimeout();
  return device.startAudio();
}

async function stopConversation() {
  return enterStandby(true);
}

async function enterStandby(stopAll = false) {
  if (registry.hasDevice()) {
    if (stopAll) device.stop();
    else device.stopAudio();
  }
  closeVoiceSessions();
  showStandby();
  return { standby: true };
}

function handleTap() {
  const now = Date.now();
  const isDoubleTap = now - lastTapAt <= DOUBLE_TAP_MS;
  lastTapAt = now;
  console.log(`[voice] ${isDoubleTap ? "double tap" : "tap"} received`);

  const action = isDoubleTap ? enterStandby(true) : isConversationActive ? undefined : startConversation();
  if (!action) return;

  action.catch((error) => {
    registry.handleDeviceMessage({ type: "error", message: error instanceof Error ? error.message : String(error) });
  });
}

async function processTurn(manual = false) {
  if (isTurnInProgress) {
    console.log("[turn] already in progress, keeping pending transcript");
    return;
  }
  isTurnInProgress = true;
  let handedToSpeech = false;

  try {
    clearListenTimeout();
    device.stopAudio();
    device.screen("Thinking...", "thinking");

    const transcript = pendingTranscript.trim();
    pendingTranscript = "";
    console.log("[turn] transcript:", JSON.stringify(transcript));

    if (!transcript) {
      console.log("[turn] no transcript, going ready");
      await enterStandby();
      return;
    }

    console.log("[turn] agent start");
    const turnT0 = Date.now();
    const messages: ModelMessage[] = [...conversationMessages, { role: "user", content: transcript }];
    await ensureTts();
    const speech = ttsSession!.speakStream(streamStackyAgentText(messages));
    activeSpeechId = speech.id;
    activeSpeechFinished = false;
    earlySpeechDone = false;
    console.log(`[turn ${speech.id}] tts start:`, speech.url);
    liveAudioStreams.set(speech.id, speech.stream);
    handedToSpeech = true;

    speech.started.then(() => {
      if (activeSpeechId !== speech.id) return;
      device.screen("Speaking...", "speaking");
      console.log(`[turn ${speech.id}] device speak start`);
      device.speak("", speech.url);
    }).catch((error) => {
      if (activeSpeechId !== speech.id) return;
      console.error(`[turn ${speech.id}] speech start error:`, error instanceof Error ? error.message : String(error));
    });

    speech.text.then((fullText) => {
      console.log(`[turn ${speech.id}] agent finished`);
      console.log(`[turn ${speech.id}] agent streamed text:`, JSON.stringify(fullText));
      rememberTurn(transcript, fullText);
    }).catch((error) => {
      console.error(`[turn ${speech.id}] agent stream error:`, error instanceof Error ? error.message : String(error));
    });
    speech.done.catch((error) => {
      if (activeSpeechId !== speech.id) return;
      console.error(`[turn ${speech.id}] tts stream error:`, error instanceof Error ? error.message : String(error));
      device.screen(error instanceof Error ? error.message : "TTS stream failed", "error");
      isConversationActive = false;
      isTurnInProgress = false;
    });
    speech.done.then(() => {
      if (activeSpeechId !== speech.id) return;
      console.log(`[turn ${speech.id}] turn total: ${Date.now() - turnT0}ms`);
      activeSpeechFinished = true;
      if (earlySpeechDone) finishSpeechTurn();
    });
    withTimeout(speech.done, TURN_TIMEOUT_MS, "Agent/TTS stream").catch((error) => {
      if (activeSpeechId !== speech.id) return;
      console.error(`[turn ${speech.id}] stream timeout/error:`, error instanceof Error ? error.message : String(error));
      liveAudioStreams.delete(speech.id);
      ttsSession?.cancelStream(error instanceof Error ? error : new Error(String(error)));
      device.screen(error instanceof Error ? error.message : "Stream failed", "error");
      isConversationActive = false;
      isTurnInProgress = false;
      activeSpeechId = undefined;
      activeSpeechFinished = false;
      earlySpeechDone = false;
    });
  } catch (error) {
    console.error("[turn] error:", error instanceof Error ? error.message : String(error));
    device.screen(error instanceof Error ? error.message : "Error", "error");
    isConversationActive = false;
  } finally {
    if (!handedToSpeech) {
      isTurnInProgress = false;
      if (isConversationActive) await enterStandby();
    }
  }
}

function handleSpeechDone() {
  console.log("[turn] device speechDone");
  if (activeSpeechId && !activeSpeechFinished) {
    console.log(`[turn ${activeSpeechId}] ignoring early speechDone; stream still active`);
    earlySpeechDone = true;
    return;
  }
  finishSpeechTurn();
}

function finishSpeechTurn() {
  isTurnInProgress = false;
  activeSpeechId = undefined;
  activeSpeechFinished = false;
  earlySpeechDone = false;
  if (!isConversationActive) {
    enterStandby().catch((error) => {
      registry.handleDeviceMessage({ type: "error", message: error instanceof Error ? error.message : String(error) });
    });
    return;
  }

  device.screen("Listening...", "listening");
  device.led("#33cc99");
  device.startAudio();
  scheduleListenTimeout();
}

export function createServer() {
  return Bun.serve<StackyWsData>({
    hostname: config.host,
    port: config.port,
    idleTimeout: 60,
    async fetch(req, server) {
      const url = new URL(req.url);

      if (url.pathname === "/stacky/device") {
        const token = url.searchParams.get("token") ?? req.headers.get("x-stacky-token");
        if (token !== config.deviceToken) return new Response("unauthorized", { status: 401 });
        const ok = server.upgrade(req, { data: { kind: "device", authed: true } });
        return ok ? undefined : new Response("upgrade failed", { status: 400 });
      }

      if (url.pathname === "/stacky/debug") {
        const ok = server.upgrade(req, { data: { kind: "debug" } });
        return ok ? undefined : new Response("upgrade failed", { status: 400 });
      }

      try {
        if (req.method === "GET" && url.pathname === "/") return new Response(debugPage(), { headers: { "Content-Type": "text/html; charset=utf-8" } });
        if (req.method === "GET" && url.pathname === "/favicon.ico") return new Response(null, { status: 204 });
        if (req.method === "GET" && url.pathname === "/health") return json({ ok: true, config: healthConfig(), device: registry.stateSnapshot() });
        if (req.method === "GET" && url.pathname.startsWith("/audio/")) {
          const id = decodeURIComponent(url.pathname.slice("/audio/".length));
          const liveStream = liveAudioStreams.get(id);
          if (liveStream) {
            liveAudioStreams.delete(id);
            console.log(`[turn ${id}] live audio fetch start`);
            return new Response(liveStream, { headers: { "Content-Type": "audio/L16; rate=24000; channels=1", "Cache-Control": "no-store" } });
          }
          return new Response("not found", { status: 404 });
        }
        if (req.method === "POST" && url.pathname === "/api/prompt") {
          const body = await req.json() as { prompt?: string };
          if (!body.prompt?.trim()) return json({ error: "prompt is required" }, 400);
          const results: string[] = [];
          for await (const chunk of streamStackyAgentText(body.prompt.trim())) {
            results.push(chunk);
          }
          return json({ text: results.join("") });
        }
        if (req.method === "POST" && url.pathname === "/api/voice/start") return json(await startConversation());
        if (req.method === "POST" && url.pathname === "/api/voice/stop") return json(await stopConversation());
        if (req.method === "POST" && url.pathname === "/api/command") {
          const body = await req.json() as Record<string, unknown>;
          const type = body.type;
          if (type === "lookAt") {
            const direction = String(body.direction ?? "center");
            const poses = { left: [-30, 35], right: [30, 35], up: [0, 60], down: [0, 20], center: [0, 35] } as const;
            const pose = poses[direction as keyof typeof poses] ?? poses.center;
            return json(device.look(pose[0], pose[1], 0.5));
          }
          if (type === "look") return json(device.look(Number(body.yaw ?? 0), Number(body.pitch ?? 35), Number(body.speed ?? 0.5)));
          if (type === "face") return json(device.face(String(body.emotion ?? "neutral") as never));
          if (type === "screen") return json(device.screen(String(body.text ?? ""), String(body.mode ?? "connected") as never));
          if (type === "led") return json(device.led(String(body.color ?? "#33cc99")));
          if (type === "home") return json(device.home());
          if (type === "stop") return json(device.stop());
          if (type === "speak") return json(device.speak(String(body.text ?? ""), typeof body.audioUrl === "string" ? body.audioUrl : undefined));
          if (type === "captureImage") return json(device.captureImage(undefined, Boolean(body.enhance)));
          if (type === "volume") return json(device.volume(Number(body.volume ?? registry.getVolume())));
          if (type === "startAudio") return json(await startConversation());
          if (type === "stopAudio") return json(await stopConversation());
          if (type === "avatarJson") {
            const features: Record<string, unknown> = {};
            for (const key of ["leftEye", "rightEye", "mouth"]) {
              const val = body[key];
              if (val && typeof val === "object" && !Array.isArray(val)) {
                features[key] = val;
              }
            }
            return json(device.avatarJson(features as never));
          }
          if (type === "decorator") {
            const action = body.action === "clear" ? "clear" : "add";
            const name = typeof body.name === "string" ? body.name : undefined;
            const durationMs = typeof body.durationMs === "number" ? body.durationMs : undefined;
            const animationIntervalMs = typeof body.animationIntervalMs === "number" ? body.animationIntervalMs : undefined;
            return json(device.decorator(action, name as never, durationMs, animationIntervalMs));
          }
          return json(device.sendManualCommand(body));
        }
        return new Response("not found", { status: 404 });
      } catch (error) {
        return json({ error: error instanceof Error ? error.message : String(error) }, 500);
      }
    },
    websocket: {
      open(ws) {
        if (ws.data.kind === "debug") registry.attachDebug(ws);
        if (ws.data.kind === "device") {
          registry.attachDevice(ws);
          device.volume(registry.getVolume());
          showStandby();
        }
      },
      async message(ws, message) {
        if (ws.data.kind !== "device") return;
        if (typeof message !== "string") {
          const bytes = new Uint8Array(message as unknown as ArrayBufferLike);
          handleBinaryDeviceMessage(bytes);
          return;
        }
        try {
          const parsed = parseDeviceMessage(message);
          registry.handleDeviceMessage(parsed);
          if (parsed.type === "hello") showStandby();
          if (parsed.type === "event") {
            const event = (parsed as { event?: string }).event;
            if (event === "speechDone") handleSpeechDone();
            if (event === "tap") handleTap();
          }
        } catch (error) {
          ws.send(JSON.stringify({ type: "error", message: error instanceof Error ? error.message : String(error) }));
        }
      },
      close(ws) {
        if (ws.data.kind === "debug") registry.detachDebug(ws);
        if (ws.data.kind === "device") {
          registry.detachDevice(ws);
          closeVoiceSessions();
        }
      },
    },
  });
}
