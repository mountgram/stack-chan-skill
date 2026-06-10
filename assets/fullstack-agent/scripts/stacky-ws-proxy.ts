import type { ServerWebSocket } from "bun";

type Frame = string | Uint8Array;
type ProxyData = {
  id: string;
  target?: WebSocket;
  targetOpen: boolean;
  closed: boolean;
  pending: Frame[];
};

const host = Bun.env.STACKY_PROXY_HOST ?? "0.0.0.0";
const port = numberFromEnv("STACKY_PROXY_PORT", 6002);
const targetUrl = Bun.env.STACKY_PROXY_TARGET_WS_URL ?? Bun.env.STACKY_DEVICE_WS_URL;
const maxPendingFrames = numberFromEnv("STACKY_PROXY_MAX_PENDING_FRAMES", 64);

if (!targetUrl) {
  throw new Error("Set STACKY_PROXY_TARGET_WS_URL=ws://STACKCHAN_HOST:6001/stacky/device");
}

let nextConnectionId = 1;

const server = Bun.serve<ProxyData>({
  hostname: host,
  port,
  idleTimeout: 0,
  fetch(req, server) {
    const url = new URL(req.url);

    if (req.method === "GET" && url.pathname === "/health") {
      return Response.json({ ok: true, targetUrl });
    }

    if (url.pathname !== "/stacky/device") {
      return new Response("not found", { status: 404 });
    }

    const id = `proxy-${nextConnectionId++}`;
    const upgraded = server.upgrade(req, {
      data: { id, targetOpen: false, closed: false, pending: [] },
    });
    return upgraded ? undefined : new Response("upgrade failed", { status: 400 });
  },
  websocket: {
    open(ws) {
      const target = new WebSocket(targetUrl);
      ws.data.target = target;
      target.binaryType = "arraybuffer";

      target.addEventListener("open", () => {
        if (ws.data.closed) {
          closeTarget(target);
          return;
        }
        ws.data.targetOpen = true;
        console.log(`[${ws.data.id}] connected ${targetUrl}`);
        for (const frame of ws.data.pending.splice(0)) target.send(frame);
      });

      target.addEventListener("message", (event) => {
        if (ws.data.closed) return;
        try {
          ws.send(toFrame(event.data));
        } catch {
          closePair(ws, 1011, "proxy send failed");
        }
      });

      target.addEventListener("close", () => {
        closePair(ws, 1000, "target closed");
      });

      target.addEventListener("error", () => {
        closePair(ws, 1011, "target error");
      });
    },

    message(ws, message) {
      const frame = toFrame(message);
      const target = ws.data.target;

      if (!target || ws.data.closed) return;
      if (ws.data.targetOpen && target.readyState === WebSocket.OPEN) {
        target.send(frame);
        return;
      }

      if (target.readyState === WebSocket.CONNECTING && ws.data.pending.length < maxPendingFrames) {
        ws.data.pending.push(frame);
        return;
      }

      closePair(ws, 1011, "target unavailable");
    },

    close(ws) {
      closePair(ws, 1000, "client closed");
    },
  },
});

console.log(`Stacky WS proxy listening on ws://${host}:${port}/stacky/device`);
console.log(`Forwarding to ${targetUrl}`);

function closePair(ws: ServerWebSocket<ProxyData>, code: number, reason: string) {
  if (ws.data.closed) return;
  ws.data.closed = true;
  console.log(`[${ws.data.id}] closing: ${reason}`);
  closeTarget(ws.data.target);
  try {
    ws.close(code, reason);
  } catch {
    // Already closed.
  }
}

function closeTarget(target: WebSocket | undefined) {
  if (!target) return;
  try {
    target.close();
  } catch {
    // Already closed.
  }
}

function toFrame(data: unknown): Frame {
  if (typeof data === "string") return data;
  if (data instanceof Uint8Array) return data;
  if (data instanceof ArrayBuffer) return new Uint8Array(data);
  if (ArrayBuffer.isView(data)) return new Uint8Array(data.buffer, data.byteOffset, data.byteLength);
  throw new Error(`unsupported websocket frame: ${typeof data}`);
}

function numberFromEnv(name: string, fallback: number) {
  const raw = Bun.env[name];
  if (!raw) return fallback;
  const parsed = Number(raw);
  return Number.isFinite(parsed) ? parsed : fallback;
}
