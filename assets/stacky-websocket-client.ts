/*
 * StackChan WebSocket protocol brain-client example.
 *
 * StackChan hosts /stacky/device. This connects to the device-hosted WebSocket,
 * receives device-to-brain messages, sends brain-to-device commands, and shows
 * the binary audio/camera packet formats used by assets/app_remote_agent/.
 *
 * Run with Bun:
 *   STACKY_DEVICE_WS_URL="ws://STACKCHAN_IP:6001/stacky/device" \
 *     bun run assets/stacky-websocket-client.ts
 */

type DeviceMode = "offline" | "connecting" | "connected" | "standby" | "listening" | "thinking" | "speaking" | "error";
type FaceEmotion = "none" | "neutral" | "happy" | "angry" | "sad" | "doubt" | "sleepy";
type LedPattern = "solid" | "pulse" | "off";

type DeviceToBrainMessage =
  | { type: "hello"; id: string; version: number; capabilities: string[] }
  | { type: "telemetry"; battery: number; charging: boolean; wifiRssi: number; pose: { yaw: number; pitch: number }; volume: number }
  | { type: "event"; event: "tap" | "hold" | "stop" | "speechStart" | "speechDone" | "speechInterrupted" | "bargeIn" | "wakeWord" | "cameraImage" | string; at?: number; requestId?: string; playbackId?: string; mediaType?: string; width?: number; height?: number }
  | { type: "ack"; requestId: string; ok: true }
  | { type: "error"; requestId?: string; message: string };

type AvatarFeature = {
  x?: number;
  y?: number;
  rotation?: number;
  weight?: number;
  size?: number;
};

type BrainToDeviceCommand =
  | { type: "screen"; requestId: string; mode?: DeviceMode; text: string }
  | { type: "face"; requestId: string; emotion: FaceEmotion }
  | { type: "look"; requestId: string; yaw?: number; pitch?: number; speed?: number }
  | { type: "led"; requestId: string; color: string; pattern?: LedPattern }
  | { type: "speak"; requestId: string; text: string; playbackId?: string; audioTransport?: "websocket"; sampleRate?: 24000; bargeIn?: boolean }
  | { type: "startAudio"; requestId: string }
  | { type: "stopAudio"; requestId: string }
  | { type: "standby"; requestId: string; text?: string; wakeWord?: { enabled: boolean; phrase?: string; modelId?: string; modelUrl?: string } }
  | { type: "captureImage"; requestId: string; enhance?: boolean; preview?: boolean }
  | { type: "volume"; requestId: string; volume: number }
  | { type: "stop"; requestId: string; target?: "all" | "playback" | "speech" | "motion" }
  | { type: "playbackClear"; requestId: string }
  | { type: "home"; requestId: string }
  | { type: "ping"; requestId: string; at: number }
  | { type: "avatarJson"; requestId: string; leftEye?: AvatarFeature; rightEye?: AvatarFeature; mouth?: AvatarFeature }
  | { type: "decorator"; requestId: string; action: "add" | "clear"; name?: "heart" | "angry" | "sweat" | "shy" | "dizzy"; durationMs?: number; animationIntervalMs?: number };

const PACKET_AUDIO_PCM = 0x31;
const PACKET_CAMERA_JPEG = 0x32;
const PACKET_AUDIO_PLAYBACK_PCM = 0x41;
const PACKET_AUDIO_PLAYBACK_END = 0x42;
const wsUrl = process.env.STACKY_DEVICE_WS_URL ?? "ws://127.0.0.1:6001/stacky/device";

let pendingCameraImage: { requestId: string; mediaType: string; width?: number; height?: number } | undefined;
let nextRequestId = 1;

const ws = new WebSocket(wsUrl);
ws.binaryType = "arraybuffer";

ws.addEventListener("open", () => {
  console.log(`connected ${wsUrl}`);
});

ws.addEventListener("message", (event) => {
  if (typeof event.data !== "string") {
    handleBinary(new Uint8Array(event.data as ArrayBufferLike));
    return;
  }

  const message = JSON.parse(event.data) as DeviceToBrainMessage;
  console.log("device", message);

  if (message.type === "hello") {
    send({ type: "volume", requestId: requestId(), volume: 70 });
    send({ type: "screen", requestId: requestId(), mode: "standby", text: "Brain connected" });
    send({ type: "led", requestId: requestId(), color: "#33cc99", pattern: "solid" });
  }

  if (message.type === "event" && message.event === "cameraImage") {
    pendingCameraImage = {
      requestId: message.requestId ?? "",
      mediaType: message.mediaType ?? "image/jpeg",
      width: message.width,
      height: message.height,
    };
  }

  if (message.type === "event" && message.event === "tap") {
    send({ type: "screen", requestId: requestId(), mode: "thinking", text: "Tap received" });
  }
});

ws.addEventListener("close", () => {
  console.log("disconnected");
});

ws.addEventListener("error", (event) => {
  console.error("websocket error", event);
});

function send(command: BrainToDeviceCommand) {
  console.log("command", command);
  ws.send(JSON.stringify(command));
}

function handleBinary(bytes: Uint8Array) {
  if (pendingCameraImage) {
    console.log("camera image", { ...pendingCameraImage, bytes: bytes.byteLength });
    pendingCameraImage = undefined;
    return;
  }

  if (bytes.length < 5) {
    console.log("binary", bytes.byteLength);
    return;
  }

  const type = bytes[0];
  const length = ((bytes[1] ?? 0) << 24) | ((bytes[2] ?? 0) << 16) | ((bytes[3] ?? 0) << 8) | (bytes[4] ?? 0);
  if (type === PACKET_AUDIO_PCM) console.log("audio pcm", length);
  else if (type === PACKET_CAMERA_JPEG) console.log("legacy camera packet", length);
  else console.log("unknown binary packet", { type, length });
}

function requestId() {
  return `cmd-${nextRequestId++}`;
}
