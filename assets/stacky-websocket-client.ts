/*
 * StackChan WebSocket protocol client example.
 *
 * This file acts like a StackChan device. It connects to the brain server's
 * /stacky/device WebSocket, sends the device-to-server messages, receives every
 * server-to-device command, and illustrates the binary audio/camera packet
 * formats used by assets/app_remote_agent/.
 *
 * Run with Bun:
 *   STACKY_WS_URL="ws://LAN_HOST:6001/stacky/device?token=dev-token-change-me" \
 *     bun run assets/stacky-websocket-client.ts
 */

type DeviceMode = "offline" | "connecting" | "connected" | "listening" | "thinking" | "speaking" | "error";
type FaceEmotion = "none" | "neutral" | "happy" | "angry" | "sad" | "doubt" | "sleepy";
type LedPattern = "solid" | "pulse" | "off";

type DeviceToServerMessage =
  | { type: "hello"; id: string; version: number; capabilities: string[] }
  | { type: "telemetry"; battery: number; charging: boolean; wifiRssi: number; pose: { yaw: number; pitch: number }; volume: number }
  | { type: "event"; event: "tap" | "hold" | "stop" | "speechDone" | string; at?: number }
  | { type: "ack"; requestId: string; ok: true }
  | { type: "error"; requestId: string; message: string };

type AvatarFeature = {
  x?: number;
  y?: number;
  rotation?: number;
  weight?: number;
  size?: number;
};

type ServerToDeviceCommand =
  | { type: "screen"; requestId: string; mode?: DeviceMode; text: string }
  | { type: "face"; requestId: string; emotion: FaceEmotion }
  | { type: "look"; requestId: string; yaw?: number; pitch?: number; speed?: number }
  | { type: "led"; requestId: string; color: string; pattern?: LedPattern }
  | { type: "speak"; requestId: string; text: string; audioUrl?: string }
  | { type: "startAudio"; requestId: string }
  | { type: "stopAudio"; requestId: string }
  | { type: "captureImage"; requestId: string; enhance?: boolean }
  | { type: "volume"; requestId: string; volume: number }
  | { type: "stop"; requestId: string; target?: "all" | "speech" | "motion" }
  | { type: "home"; requestId: string }
  | { type: "ping"; requestId: string; at: number }
  | { type: "avatarJson"; requestId: string; leftEye?: AvatarFeature; rightEye?: AvatarFeature; mouth?: AvatarFeature }
  | { type: "decorator"; requestId: string; action: "add" | "clear"; name?: "heart" | "angry" | "sweat" | "shy" | "dizzy"; durationMs?: number; animationIntervalMs?: number };

const PACKET_AUDIO_PCM = 0x31;
const PACKET_CAMERA_JPEG = 0x32;

const wsUrl = process.env.STACKY_WS_URL ?? "ws://127.0.0.1:6001/stacky/device?token=dev-token-change-me";

let yaw = 0;
let pitch = 35;
let volume = 70;
let audioTimer: Timer | undefined;
let telemetryTimer: Timer | undefined;

const ws = new WebSocket(wsUrl);

ws.binaryType = "arraybuffer";

ws.addEventListener("open", () => {
  console.log(`connected ${wsUrl}`);
  sendJson({
    type: "hello",
    id: "stacky-client-example",
    version: 1,
    capabilities: ["screen", "face", "look", "led", "telemetry", "tap", "audio", "camera", "volume", "avatarJson", "decorator"],
  });
  sendTelemetry();
  telemetryTimer = setInterval(sendTelemetry, 3000);
});

ws.addEventListener("message", async (event) => {
  if (typeof event.data !== "string") {
    console.log("server sent binary data", event.data);
    return;
  }

  let command: ServerToDeviceCommand;
  try {
    command = JSON.parse(event.data) as ServerToDeviceCommand;
  } catch {
    sendJson({ type: "error", requestId: "", message: "invalid json" });
    return;
  }

  console.log("command", command);
  await handleCommand(command);
});

ws.addEventListener("close", () => {
  console.log("disconnected");
  if (telemetryTimer) clearInterval(telemetryTimer);
  if (audioTimer) clearInterval(audioTimer);
});

ws.addEventListener("error", (event) => {
  console.error("websocket error", event);
});

async function handleCommand(command: ServerToDeviceCommand) {
  switch (command.type) {
    case "screen":
      console.log(`screen ${command.mode ?? "connected"}: ${command.text}`);
      ack(command.requestId);
      return;

    case "face":
      console.log(`face ${command.emotion}`);
      ack(command.requestId);
      return;

    case "look":
      yaw = clamp(command.yaw ?? yaw, -128, 128);
      pitch = clamp(command.pitch ?? pitch, 5, 85);
      console.log(`look yaw=${yaw} pitch=${pitch} speed=${command.speed ?? 0.5}`);
      ack(command.requestId);
      return;

    case "led":
      console.log(`led color=${command.color} pattern=${command.pattern ?? "solid"}`);
      ack(command.requestId);
      return;

    case "speak":
      console.log(`speak text=${command.text} audioUrl=${command.audioUrl ?? "none"}`);
      ack(command.requestId);
      await delay(500);
      sendJson({ type: "event", event: "speechDone" });
      return;

    case "startAudio":
      ack(command.requestId);
      startAudioStream();
      return;

    case "stopAudio":
      stopAudioStream();
      ack(command.requestId);
      return;

    case "captureImage":
      ack(command.requestId);
      sendCameraImage(command.requestId);
      return;

    case "volume":
      volume = clamp(command.volume, 0, 100);
      ack(command.requestId);
      return;

    case "stop":
      stopAudioStream();
      console.log(`stop target=${command.target ?? "all"}`);
      ack(command.requestId);
      return;

    case "home":
      yaw = 0;
      pitch = 35;
      ack(command.requestId);
      return;

    case "ping":
      ack(command.requestId);
      return;

    case "avatarJson":
      console.log("avatarJson", JSON.stringify(command));
      ack(command.requestId);
      return;

    case "decorator":
      console.log(`decorator action=${command.action} name=${command.name ?? "none"}`);
      ack(command.requestId);
      return;
  }
}

function sendTelemetry() {
  sendJson({
    type: "telemetry",
    battery: 82,
    charging: false,
    wifiRssi: -55,
    pose: { yaw, pitch },
    volume,
  });
}

function startAudioStream() {
  if (audioTimer) return;
  audioTimer = setInterval(() => {
    sendPacket(PACKET_AUDIO_PCM, makeSilentPcm(512));
  }, 100);
}

function stopAudioStream() {
  if (!audioTimer) return;
  clearInterval(audioTimer);
  audioTimer = undefined;
}

function sendCameraImage(requestId: string) {
  const metadata = new TextEncoder().encode(JSON.stringify({ requestId, width: 1, height: 1, mediaType: "image/jpeg" }));
  const jpeg = new Uint8Array([0xff, 0xd8, 0xff, 0xd9]);
  const payload = new Uint8Array(metadata.length + jpeg.length);
  payload.set(metadata, 0);
  payload.set(jpeg, metadata.length);
  sendPacket(PACKET_CAMERA_JPEG, payload, metadata.length);
}

function sendPacket(type: number, payload: Uint8Array, lengthOverride = payload.length) {
  const packet = new Uint8Array(5 + payload.length);
  packet[0] = type;
  packet[1] = (lengthOverride >> 24) & 0xff;
  packet[2] = (lengthOverride >> 16) & 0xff;
  packet[3] = (lengthOverride >> 8) & 0xff;
  packet[4] = lengthOverride & 0xff;
  packet.set(payload, 5);
  ws.send(packet);
}

function makeSilentPcm(frames: number) {
  return new Uint8Array(frames * 2);
}

function ack(requestId: string) {
  sendJson({ type: "ack", requestId, ok: true });
}

function sendJson(message: DeviceToServerMessage) {
  ws.send(JSON.stringify(message));
}

function clamp(value: number, min: number, max: number) {
  return Math.min(max, Math.max(min, value));
}

function delay(ms: number) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}
