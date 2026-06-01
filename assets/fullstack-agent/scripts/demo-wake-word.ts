process.env.STACKY_SERVER_HOST = "127.0.0.1";
process.env.STACKY_SERVER_PORT = String(20_000 + Math.floor(Math.random() * 20_000));
process.env.STACKY_DEVICE_TOKEN = "demo-token";
process.env.STACKY_WAKE_WORD_ENABLED = "true";
process.env.STACKY_WAKE_WORD_PHRASE = "Stacky";
process.env.STACKY_WAKE_WORD_MODEL_ID = "stacky";
process.env.STACKY_VOICE_MOCK = "true";

const { createServer } = await import("../src/server/routes");

type Command = {
  type: string;
  requestId?: string;
  text?: string;
  wakeWord?: { enabled: boolean; phrase?: string; modelId?: string };
};

const server = createServer();
const wsUrl = new URL(server.url);
wsUrl.protocol = wsUrl.protocol === "https:" ? "wss:" : "ws:";
wsUrl.pathname = "/stacky/device";
wsUrl.search = "?token=demo-token";

const seen: string[] = [];
let wakeWordArmed = false;
let startedAudio = false;

function send(ws: WebSocket, value: unknown) {
  ws.send(JSON.stringify(value));
}

const done = new Promise<void>((resolve, reject) => {
  const timeout = setTimeout(() => reject(new Error(`demo timed out; saw: ${seen.join(", ")}`)), 5000);
  const ws = new WebSocket(wsUrl);

  ws.addEventListener("open", () => {
    send(ws, {
      type: "hello",
      id: "stacky-demo",
      version: 2,
      capabilities: ["screen", "face", "led", "audio", "standby", "wakeWord"],
      wakeWord: {
        version: 1,
        models: [{ id: "stacky", phrase: "Stacky", source: "firmware" }],
        dynamicModels: false,
      },
    });
  });

  ws.addEventListener("message", (event) => {
    if (typeof event.data !== "string") return;
    const command = JSON.parse(event.data) as Command;
    seen.push(command.type);
    if (command.requestId) send(ws, { type: "ack", requestId: command.requestId, ok: true });

    if (command.type === "standby" && command.wakeWord?.phrase === "Stacky" && !wakeWordArmed) {
      wakeWordArmed = true;
      console.log("standby armed:", JSON.stringify(command));
      send(ws, {
        type: "event",
        event: "wakeWord",
        wakeWord: "Stacky",
        phrase: "Stacky",
        modelId: "stacky",
        score: 1,
        at: Date.now(),
      });
    }

    if (command.type === "startAudio") {
      startedAudio = true;
      console.log("conversation started:", JSON.stringify(command));
      clearTimeout(timeout);
      ws.close();
      resolve();
    }
  });

  ws.addEventListener("error", () => {
    clearTimeout(timeout);
    reject(new Error("websocket error"));
  });
});

try {
  await done;
  if (!wakeWordArmed || !startedAudio) throw new Error("wake-word demo did not complete");
  console.log("wake-word demo passed");
} finally {
  server.stop(true);
}
