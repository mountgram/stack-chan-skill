import { commandId, type AvatarFeature, type DecoratorName, type DeviceMode, type FaceEmotion, type WakeWordConfig } from "./protocol";
import { clampPitch, clampSpeed, clampVolume, clampYaw } from "./safety";
import { registry } from "./registry";

export function screen(text: string, mode: DeviceMode = "connected") {
  return registry.send({ type: "screen", requestId: commandId(), mode, text: text.slice(0, 180) });
}

export function face(emotion: FaceEmotion) {
  return registry.send({ type: "face", requestId: commandId(), emotion });
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

export function led(color: string) {
  return registry.send({ type: "led", requestId: commandId(), color });
}

export function speak(text: string, audioUrl?: string) {
  return registry.send({ type: "speak", requestId: commandId(), text: text.slice(0, 500), audioUrl });
}

export function startAudio() {
  return registry.send({ type: "startAudio", requestId: commandId() });
}

export function stopAudio() {
  return registry.send({ type: "stopAudio", requestId: commandId() });
}

export function standby(text = "Standby. Tap to talk.", wakeWord?: WakeWordConfig) {
  return registry.send({ type: "standby", requestId: commandId(), text: text.slice(0, 180), wakeWord });
}

export function captureImage(requestId = commandId("img"), enhance = false) {
  return registry.send({ type: "captureImage", requestId, enhance });
}

export function volume(value: number) {
  const next = clampVolume(value);
  const command = { type: "volume" as const, requestId: commandId(), volume: next };
  registry.setVolume(next);
  if (!registry.hasDevice()) return command;
  return registry.send(command);
}

export function stop(target: "all" | "speech" | "motion" = "all") {
  return registry.send({ type: "stop", requestId: commandId(), target });
}

export function home() {
  return registry.send({ type: "home", requestId: commandId() });
}

export function avatarJson(features: {
  leftEye?: AvatarFeature;
  rightEye?: AvatarFeature;
  mouth?: AvatarFeature;
}) {
  return registry.send({ type: "avatarJson", requestId: commandId(), ...features });
}

export function decorator(action: "add" | "clear", name?: DecoratorName, durationMs?: number, animationIntervalMs?: number) {
  return registry.send({ type: "decorator", requestId: commandId(), action, name, durationMs, animationIntervalMs });
}

export function sendManualCommand(command: Record<string, unknown>) {
  const type = typeof command.type === "string" ? command.type : "screen";
  const requestId = typeof command.requestId === "string" ? command.requestId : commandId();
  return registry.send({ ...command, type, requestId } as never);
}
