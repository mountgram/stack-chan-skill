import { DeepgramClient } from "@deepgram/sdk";
import { WebSocket as NodeWebSocket } from "ws";
import { config } from "../config";

type LiveConnection = Awaited<ReturnType<DeepgramClient["listen"]["v2"]["connect"]>>;

export type LiveSttEvent =
  | { type: "open" }
  | { type: "close" }
  | { type: "error"; message: string }
  | { type: "connected"; raw: unknown }
  | { type: "turn"; event: string; transcript: string; raw: unknown };

export class DeepgramLiveSession {
  private connection?: LiveConnection;
  private pingTimer?: Timer;
  private open = false;

  constructor(private onEvent: (event: LiveSttEvent) => void) {}

  async start() {
    if (!config.deepgramApiKey) throw new Error("DEEPGRAM_API_KEY is required for live STT");

    (globalThis as unknown as { WebSocket: unknown }).WebSocket = NodeWebSocket;
    const client = new DeepgramClient({ apiKey: config.deepgramApiKey });
    const connection = await client.listen.v2.connect({
      model: "flux-general-en",
      encoding: "linear16",
      sample_rate: 24000,
      Authorization: `Token ${config.deepgramApiKey}`,
    });
    connection.socket.binaryType = "arraybuffer";
    this.connection = connection;

    connection.on("open", () => {
      this.open = true;
      this.onEvent({ type: "open" });
    });
    connection.on("message", (message: { type?: string; event?: string; transcript?: string }) => {
      if (message.type === "Connected") {
        this.onEvent({ type: "connected", raw: message });
        return;
      }
      if (message.type === "TurnInfo") {
        this.onEvent({ type: "turn", event: message.event ?? "Update", transcript: message.transcript ?? "", raw: message });
        return;
      }
      if (message.type === "FatalError") {
        this.onEvent({ type: "error", message: JSON.stringify(message) });
      }
    });
    connection.on("error", (error: Error) => this.onEvent({ type: "error", message: error.message }));
    connection.on("close", () => {
      this.open = false;
      if (this.pingTimer) clearInterval(this.pingTimer);
      this.pingTimer = undefined;
      this.onEvent({ type: "close" });
    });

    connection.connect();
    await connection.waitForOpen();
  }

  sendAudio(audio: ArrayBufferView) {
    if (!this.open || !this.connection) return false;
    this.connection.sendMedia(audio);
    return true;
  }

  closeStream() {
    this.connection?.sendCloseStream({ type: "CloseStream" });
  }

  close() {
    if (this.pingTimer) clearInterval(this.pingTimer);
    this.pingTimer = undefined;
    this.open = false;
    this.connection?.close();
    this.connection = undefined;
  }
}
