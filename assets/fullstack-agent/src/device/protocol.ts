export type DeviceMode = "offline" | "connecting" | "connected" | "standby" | "listening" | "thinking" | "speaking" | "error";
export type FaceEmotion = "none" | "neutral" | "happy" | "angry" | "sad" | "doubt" | "sleepy";

export type WakeWordModelInfo = {
  id: string;
  phrase?: string;
  source?: "firmware" | "download";
};

export type DeviceHello = {
  type: "hello";
  id?: string;
  device?: string;
  version?: number;
  capabilities?: string[];
  wakeWord?: {
    version?: number;
    models?: WakeWordModelInfo[];
    dynamicModels?: boolean;
  };
};

export type DeviceTelemetry = {
  type: "telemetry";
  battery?: number;
  charging?: boolean;
  wifiRssi?: number;
  pose?: { yaw?: number; pitch?: number };
  volume?: number;
};

export type DeviceEvent = {
  type: "event";
  event: "tap" | "hold" | "stop" | "wakeWord" | string;
  at?: number;
  wakeWord?: string;
  phrase?: string;
  modelId?: string;
  score?: number;
};

export type DeviceAck = { type: "ack"; requestId?: string; ok?: boolean };
export type DeviceError = { type: "error"; requestId?: string; message: string };
export type DeviceMessage = DeviceHello | DeviceTelemetry | DeviceEvent | DeviceAck | DeviceError | { type: string; [key: string]: unknown };

export type ScreenCommand = { type: "screen"; requestId: string; mode?: DeviceMode; text: string };
export type FaceCommand = { type: "face"; requestId: string; emotion: FaceEmotion };
export type LookCommand = { type: "look"; requestId: string; yaw?: number; pitch?: number; speed?: number };
export type LedCommand = { type: "led"; requestId: string; color: string };
export type SpeakCommand = { type: "speak"; requestId: string; text: string; audioTransport?: "websocket"; sampleRate?: 24000 };
export type StartAudioCommand = { type: "startAudio"; requestId: string };
export type StopAudioCommand = { type: "stopAudio"; requestId: string };
export type WakeWordConfig = {
  enabled: boolean;
  phrase?: string;
  modelId?: string;
  modelUrl?: string;
};
export type StandbyCommand = { type: "standby"; requestId: string; text?: string; wakeWord?: WakeWordConfig };
export type CaptureImageCommand = { type: "captureImage"; requestId: string; enhance?: boolean; preview?: boolean };
export type VolumeCommand = { type: "volume"; requestId: string; volume: number };
export type StopCommand = { type: "stop"; requestId: string; target?: "all" | "speech" | "motion" };
export type HomeCommand = { type: "home"; requestId: string };
export type AvatarFeature = {
  x?: number;
  y?: number;
  rotation?: number;
  weight?: number;
  size?: number;
};

export type AvatarJsonCommand = {
  type: "avatarJson";
  requestId: string;
  leftEye?: AvatarFeature;
  rightEye?: AvatarFeature;
  mouth?: AvatarFeature;
};

export type DecoratorName = "heart" | "angry" | "sweat" | "shy" | "dizzy";

export type DecoratorCommand = {
  type: "decorator";
  requestId: string;
  action: "add" | "clear";
  name?: DecoratorName;
  durationMs?: number;
  animationIntervalMs?: number;
};

export type RenderCommand =
  | { type: "render.defineScene"; requestId: string; sceneId: string; size: { width: 320; height: 240 }; background?: string; nodes: unknown[] }
  | { type: "render.setScene"; requestId: string; sceneId: string }
  | { type: "render.animate"; requestId: string; animationId: string; loop?: boolean; yoyo?: boolean; tracks: unknown[] }
  | { type: "render.reset"; requestId: string };

export type DeviceCommand = ScreenCommand | FaceCommand | LookCommand | LedCommand | SpeakCommand | StartAudioCommand | StopAudioCommand | StandbyCommand | CaptureImageCommand | VolumeCommand | StopCommand | HomeCommand | AvatarJsonCommand | DecoratorCommand | RenderCommand;

export function parseDeviceMessage(raw: string): DeviceMessage {
  const value = JSON.parse(raw) as unknown;
  if (!value || typeof value !== "object" || !("type" in value) || typeof (value as { type: unknown }).type !== "string") {
    throw new Error("message must be a JSON object with a string type");
  }
  return value as DeviceMessage;
}

export function commandId(prefix = "cmd") {
  return `${prefix}-${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 8)}`;
}
