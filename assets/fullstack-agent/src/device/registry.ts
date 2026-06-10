import type { ServerWebSocket } from "bun";
import type { DeviceCommand, DeviceMessage, DeviceTelemetry } from "./protocol";

export type StackyWsData = {
  kind: "debug" | "stt";
  id?: string;
  deepgram?: WebSocket;
  transcriptParts?: string[];
  completed?: boolean;
};

export type DeviceState = {
  id: string;
  connected: boolean;
  connectedAt: number;
  lastSeenAt: number;
  capabilities: string[];
  volume: number;
  telemetry?: DeviceTelemetry;
};

export type CameraImage = {
  requestId: string;
  mediaType: string;
  width?: number;
  height?: number;
  data: Uint8Array;
};

export type CameraImageSnapshot = {
  requestId: string;
  mediaType: string;
  width?: number;
  height?: number;
  bytes: number;
  capturedAt: number;
  dataUrl: string;
};

type DebugSocket = ServerWebSocket<StackyWsData>;

type SendableDeviceSocket = {
  send(data: string | Uint8Array): unknown;
};

class Registry {
  private volume = 90;
  private device?: SendableDeviceSocket;
  private state?: DeviceState;
  private debugSockets = new Set<DebugSocket>();
  private log: unknown[] = [];
  private pendingImages = new Map<string, { resolve: (image: CameraImage) => void; reject: (error: Error) => void; timeout: Timer }>();
  private lastCameraImage?: CameraImageSnapshot;

  attachDevice(ws: SendableDeviceSocket, id = "stacky") {
    this.device = ws;
    this.state = {
      id,
      connected: true,
      connectedAt: Date.now(),
      lastSeenAt: Date.now(),
      capabilities: [],
      volume: this.volume,
    };
    this.broadcast({ type: "device-connected", id });
  }

  detachDevice(ws: SendableDeviceSocket) {
    if (this.device !== ws) return;
    this.device = undefined;
    if (this.state) {
      this.state.connected = false;
      this.state.lastSeenAt = Date.now();
    }
    this.broadcast({ type: "device-disconnected" });
  }

  attachDebug(ws: DebugSocket) {
    this.debugSockets.add(ws);
    ws.send(JSON.stringify({ type: "snapshot", state: this.stateSnapshot(), log: this.log.slice(-80), lastCameraImage: this.lastCameraImage }));
  }

  detachDebug(ws: DebugSocket) {
    this.debugSockets.delete(ws);
  }

  handleDeviceMessage(message: DeviceMessage) {
    if (this.state) this.state.lastSeenAt = Date.now();
    if (message.type === "hello") {
      const hello = message as { id?: string; device?: string; capabilities?: string[] };
      const id = hello.id ?? hello.device ?? "stacky";
      if (this.state) {
        this.state.id = id;
        this.state.capabilities = hello.capabilities ?? [];
      }
    }
    if (message.type === "telemetry" && this.state) {
      this.state.telemetry = message as DeviceTelemetry;
      if (typeof message.volume === "number") this.setVolume(message.volume);
    }
    if (message.type === "error") {
      const error = message as { requestId?: string; message?: string };
      if (error.requestId) this.rejectImage(error.requestId, new Error(error.message ?? "camera capture failed"));
    }
    this.broadcast({ type: "device-message", message });
  }

  handleCameraImage(image: CameraImage) {
    if (this.state) this.state.lastSeenAt = Date.now();
    this.lastCameraImage = {
      requestId: image.requestId,
      mediaType: image.mediaType,
      width: image.width,
      height: image.height,
      bytes: image.data.byteLength,
      capturedAt: Date.now(),
      dataUrl: `data:${image.mediaType};base64,${Buffer.from(image.data).toString("base64")}`,
    };
    const pending = this.pendingImages.get(image.requestId);
    if (pending) {
      clearTimeout(pending.timeout);
      this.pendingImages.delete(image.requestId);
      pending.resolve(image);
    }
    this.broadcast({ type: "camera-image", image: this.lastCameraImage });
  }

  waitForImage(requestId: string, timeoutMs = 10_000) {
    return new Promise<CameraImage>((resolve, reject) => {
      const timeout = setTimeout(() => {
        this.pendingImages.delete(requestId);
        reject(new Error("camera capture timed out"));
      }, timeoutMs);
      this.pendingImages.set(requestId, { resolve, reject, timeout });
    });
  }

  send(command: DeviceCommand) {
    if (!this.device) throw new Error("StackChan is not connected");
    this.device.send(JSON.stringify(command));
    this.broadcast({ type: "command", command });
    return command;
  }

  hasDevice() {
    return Boolean(this.device);
  }

  getVolume() {
    return this.volume;
  }

  setVolume(volume: number) {
    this.volume = volume;
    if (this.state) this.state.volume = volume;
  }

  stateSnapshot() {
    return this.state ?? { connected: false, volume: this.volume };
  }

  private broadcast(event: unknown) {
    this.log.push({ at: Date.now(), event });
    this.log = this.log.slice(-200);
    const payload = JSON.stringify(event);
    for (const ws of this.debugSockets) ws.send(payload);
  }

  private rejectImage(requestId: string, error: Error) {
    const pending = this.pendingImages.get(requestId);
    if (!pending) return;
    clearTimeout(pending.timeout);
    this.pendingImages.delete(requestId);
    pending.reject(error);
  }
}

export const registry = new Registry();
