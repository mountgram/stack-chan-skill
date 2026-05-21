import { describe, expect, test } from "bun:test";
import { clampPitch, clampSpeed, clampVolume, clampYaw, hexToRgb } from "./safety";

describe("device safety", () => {
  test("clamps pitch to safe servo range", () => {
    expect(clampPitch(-50)).toBe(5);
    expect(clampPitch(35)).toBe(35);
    expect(clampPitch(120)).toBe(85);
  });

  test("clamps yaw and normalized speed", () => {
    expect(clampYaw(-200)).toBe(-128);
    expect(clampYaw(200)).toBe(128);
    expect(clampSpeed(2)).toBe(1);
  });

  test("clamps speaker volume to percent range", () => {
    expect(clampVolume(-20)).toBe(0);
    expect(clampVolume(89.6)).toBe(90);
    expect(clampVolume(150)).toBe(100);
  });

  test("parses hex colors", () => {
    expect(hexToRgb("#33cc99")).toEqual({ r: 51, g: 204, b: 153 });
  });
});
