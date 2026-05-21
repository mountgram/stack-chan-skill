import { tool } from "ai";
import { z } from "zod";
import * as device from "../device/commands";
import { registry } from "../device/registry";

export const faceTool = tool({
  description: "Set StackChan's face emotion.",
  inputSchema: z.object({
    emotion: z.enum(["none", "neutral", "happy", "angry", "sad", "doubt", "sleepy"]),
  }),
  execute: async ({ emotion }) => device.face(emotion),
});

export const lookTool = tool({
  description:
    "Move StackChan's head. Yaw is -128..128, pitch is 5..85, speed is 0.1..1.",
  inputSchema: z.object({
    yaw: z.number().optional(),
    pitch: z.number().optional(),
    speed: z.number().optional(),
  }),
  execute: async ({ yaw, pitch, speed }) => device.look(yaw, pitch, speed),
});

export const ledTool = tool({
  description: "Set StackChan LEDs to a hex color. Use #000000 to turn LEDs off.",
  inputSchema: z.object({
    color: z.string().regex(/^#[0-9a-fA-F]{6}$/),
  }),
  execute: async ({ color }) => device.led(color),
});

export const avatarJsonTool = tool({
  description:
    "Fine-grained control over StackChan's facial features. Set eye/mouth position, size, rotation, and weight independently. Coordinates: x/y -100..100, rotation 0..3600, weight 0..100, size -100..100 (0=normal).",
  inputSchema: z.object({
    leftEye: z
      .object({
        x: z.number().min(-100).max(100).optional(),
        y: z.number().min(-100).max(100).optional(),
        rotation: z.number().min(0).max(3600).optional(),
        weight: z.number().min(0).max(100).optional(),
        size: z.number().min(-100).max(100).optional(),
      })
      .optional(),
    rightEye: z
      .object({
        x: z.number().min(-100).max(100).optional(),
        y: z.number().min(-100).max(100).optional(),
        rotation: z.number().min(0).max(3600).optional(),
        weight: z.number().min(0).max(100).optional(),
        size: z.number().min(-100).max(100).optional(),
      })
      .optional(),
    mouth: z
      .object({
        x: z.number().min(-100).max(100).optional(),
        y: z.number().min(-100).max(100).optional(),
        rotation: z.number().min(0).max(3600).optional(),
        weight: z.number().min(0).max(100).optional(),
        size: z.number().min(-100).max(100).optional(),
      })
      .optional(),
  }),
  execute: async (args) => device.avatarJson(args),
});

export const decoratorTool = tool({
  description:
    "Add or clear animated avatar overlay effects. These are not face emotions. Types: heart (floating hearts), angry (vein marks), sweat (sweat drops), shy (blush lines), dizzy (spiral). Duration in ms (max 30000, 0=persistent). Clear removes all active decorators.",
  inputSchema: z.object({
    action: z.enum(["add", "clear"]),
    name: z.enum(["heart", "angry", "sweat", "shy", "dizzy"]).optional(),
    durationMs: z.number().min(0).max(30000).optional(),
  }),
  execute: async ({ action, name, durationMs }) =>
    device.decorator(action, name, durationMs),
});

export const getBatteryTool = tool({
  description: "Get the latest StackChan battery telemetry.",
  inputSchema: z.object({}),
  execute: async () => {
    const state = registry.stateSnapshot();
    const telemetry = "telemetry" in state ? state.telemetry : undefined;
    return {
      connected: state.connected,
      battery: telemetry?.battery ?? null,
      charging: telemetry?.charging ?? null,
      lastSeenAt: "lastSeenAt" in state ? state.lastSeenAt : null,
      telemetryAgeMs:
        "lastSeenAt" in state ? Date.now() - state.lastSeenAt : null,
    };
  },
});
