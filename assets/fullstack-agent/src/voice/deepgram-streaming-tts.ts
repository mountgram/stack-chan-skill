import { DeepgramClient } from "@deepgram/sdk";
import { WebSocket as NodeWebSocket } from "ws";
import { config } from "../config";

const initialSilence = new Uint8Array(2400);
const silenceKeepaliveMs = 2_000;

export class DeepgramStreamingTts {
  private client: DeepgramClient;
  private connection?: Awaited<ReturnType<DeepgramClient["speak"]["v1"]["connect"]>>;
  private audioChunks: Buffer[] = [];
  private audioController?: ReadableStreamDefaultController<Uint8Array>;
  private silenceKeepalive?: Timer;
  private streamedAudioStarted = false;
  private flushed = false;
  private flushPromise?: Promise<Buffer>;
  private flushResolve?: (audio: Buffer) => void;
  private flushReject?: (error: Error) => void;
  private streamReject?: (error: Error) => void;

  constructor() {
    (globalThis as unknown as { WebSocket: unknown }).WebSocket = NodeWebSocket;
    this.client = new DeepgramClient({ apiKey: config.deepgramApiKey });
  }

  async start() {
    if (!config.deepgramApiKey) throw new Error("DEEPGRAM_API_KEY is required");

    this.connection = await this.client.speak.v1.connect({
      model: config.deepgramTtsModel,
      encoding: "linear16",
      sample_rate: "24000",
      Authorization: `Token ${config.deepgramApiKey}`,
    });

    this.connection.socket.binaryType = "arraybuffer";

    this.connection.on("open", () => {
      /* connection ready */ 
    });

    this.connection.on("message", (message: unknown) => {
      if (message instanceof ArrayBuffer) {
        this.handleAudio(Buffer.from(message));
        return;
      }
      if (message instanceof Buffer) {
        this.handleAudio(message);
        return;
      }
      if (typeof message === "string") {
        const parsed = this.parseMessage(message);
        if (parsed?.type === "Flushed") {
          this.resolveFlush();
          return;
        }
        this.handleAudio(Buffer.from(message, "base64"));
        return;
      }
      const msg = message as { type?: string };
      if (msg.type === "Flushed") {
        this.resolveFlush();
      }
    });

    this.connection.on("error", (error: Error) => {
      this.flushReject?.(error);
    });

    this.connection.on("close", () => {
      if (!this.flushed) {
        this.flushReject?.(new Error("TTS connection closed unexpectedly"));
      }
    });

    this.connection.connect();
    await this.connection.waitForOpen();
  }

  speakStream(textChunks: AsyncIterable<string>): { id: string; url: string; stream: ReadableStream<Uint8Array>; started: Promise<void>; text: Promise<string>; done: Promise<void> } {
    if (!this.connection) throw new Error("TTS not started");

    const id = `tts-${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 8)}.pcm`;
    console.log(`[turn ${id}] tts stream created`);
    const stream = new ReadableStream<Uint8Array>({
      start: (controller) => {
        this.audioController = controller;
        controller.enqueue(initialSilence);
        this.silenceKeepalive = setInterval(() => {
          try {
            controller.enqueue(initialSilence);
          } catch {
            this.clearSilenceKeepalive();
          }
        }, silenceKeepaliveMs);
        console.log(`[turn ${id}] tts audio stream opened`);
      },
      cancel: () => {
        this.clearSilenceKeepalive();
        this.audioController = undefined;
        console.log(`[turn ${id}] tts audio stream cancelled`);
      },
    });

    let resolveText!: (text: string) => void;
    let rejectText!: (error: Error) => void;
    const text = new Promise<string>((resolve, reject) => {
      resolveText = resolve;
      rejectText = reject;
    });

    let resolveStarted!: () => void;
    let rejectStarted!: (error: Error) => void;
    const started = new Promise<void>((resolve, reject) => {
      resolveStarted = resolve;
      rejectStarted = reject;
    });

    this.flushed = false;
    this.audioChunks = [];
    this.flushPromise = new Promise((resolve, reject) => {
      this.flushResolve = resolve;
      this.flushReject = reject;
    });

    const done = (async () => {
      let fullText = "";
      let speakCount = 0;
      const t0 = Date.now();
      try {
        console.log(`[turn ${id}] tts input start`);
        for await (const chunk of textChunks) {
          if (!chunk) continue;
          fullText += chunk;
          speakCount++;
          if (fullText === chunk) {
            console.log(`[turn ${id}] agent first text chunk (${Date.now() - t0}ms since tts start)`);
            resolveStarted();
          }
          this.connection!.sendText({ type: "Speak", text: chunk });
        }
        console.log(`[turn ${id}] agent finished text:`, JSON.stringify(fullText.trim()));
        console.log(`[turn ${id}] tts input finished (${speakCount} speaks, ${Date.now() - t0}ms), flushing`);
        resolveText(fullText.trim());
        const flushT0 = Date.now();
        this.connection!.sendFlush({ type: "Flush" });
        await this.flushPromise;
        console.log(`[turn ${id}] tts flushed after ${Date.now() - flushT0}ms, stream done (${Date.now() - t0}ms total)`);
      } catch (error) {
        const err = error instanceof Error ? error : new Error(String(error));
        rejectStarted(err);
        rejectText(err);
        this.cancelStream(err);
        console.error(`[turn ${id}] tts stream failed:`, err.message);
        throw err;
      }
    })();

    return { id, url: `${config.publicBaseUrl.replace(/\/$/, "")}/audio/${id}`, stream, started, text, done };
  }

  close() {
    try {
      this.connection?.sendClose({ type: "Close" });
    } catch {
      /* ignore */ 
    }
    this.connection?.close();
    this.connection = undefined;
  }

  cancelStream(error = new Error("TTS stream cancelled")) {
    this.streamReject?.(error);
    this.flushReject?.(error);
    this.clearSilenceKeepalive();
    this.audioController?.error(error);
    this.audioController = undefined;
    this.audioChunks = [];
    this.streamedAudioStarted = false;
  }

  private parseMessage(message: string) {
    try {
      return JSON.parse(message) as { type?: string };
    } catch {
      return undefined;
    }
  }

  private resolveFlush() {
    this.flushed = true;
    const pcm = Buffer.concat(this.audioChunks);
    console.log("[turn] tts flushed");
    this.audioChunks = [];
    this.clearSilenceKeepalive();
    this.audioController?.close();
    this.audioController = undefined;
    this.streamedAudioStarted = false;
    this.flushResolve?.(pcm);
  }

  private handleAudio(audio: Buffer) {
    if (this.audioController) {
      if (!this.streamedAudioStarted) {
        console.log("[turn] tts first audio chunk");
        this.streamedAudioStarted = true;
        this.clearSilenceKeepalive();
      }
      this.audioController.enqueue(audio);
      return;
    }
    this.audioChunks.push(audio);
  }

  private clearSilenceKeepalive() {
    if (!this.silenceKeepalive) return;
    clearInterval(this.silenceKeepalive);
    this.silenceKeepalive = undefined;
  }
}
