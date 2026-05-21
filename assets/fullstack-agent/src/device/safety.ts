const PITCH_MIN = 5;
const PITCH_MAX = 85;
const YAW_MIN = -128;
const YAW_MAX = 128;

function clamp(value: number, min: number, max: number) {
  return Math.min(max, Math.max(min, value));
}

export function clampPitch(value: number) {
  return clamp(Math.round(value), PITCH_MIN, PITCH_MAX);
}

export function clampYaw(value: number) {
  return clamp(Math.round(value), YAW_MIN, YAW_MAX);
}

export function clampSpeed(value: number | undefined) {
  if (value === undefined) return 0.5;
  return clamp(value, 0.1, 1);
}

export function clampVolume(value: number) {
  return clamp(Math.round(Number.isFinite(value) ? value : 90), 0, 100);
}

export function hexToRgb(hex: string) {
  const normalized = hex.trim().replace(/^#/, "");
  if (!/^[0-9a-fA-F]{6}$/.test(normalized)) {
    throw new Error("color must be a #rrggbb hex value");
  }
  return {
    r: Number.parseInt(normalized.slice(0, 2), 16),
    g: Number.parseInt(normalized.slice(2, 4), 16),
    b: Number.parseInt(normalized.slice(4, 6), 16),
  };
}
