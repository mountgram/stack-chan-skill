import { ToolLoopAgent, tool, stepCountIs, type ModelMessage } from "ai";
import { z } from "zod";
import { config } from "../config";
import * as device from "../device/commands";
import { systemPrompt } from "./prompt";
import {
  lookTool,
  faceTool,
  ledTool,
  decoratorTool,
  getBatteryTool,
} from "./tools";
import { registry } from "../device/registry";

let agent: ReturnType<typeof createAgent>;
let textOnlyAgent: any;

function createAgent() {
  const model = getModel();

  return new ToolLoopAgent({
    model,
    instructions: systemPrompt,
    stopWhen: stepCountIs(5),
    maxOutputTokens: 256,
    maxRetries: 1,
    onStepFinish: ({ stepNumber, content }) => {
      console.log("agent step: ", stepNumber, content);
    },
    tools: {
      lookAt: lookTool,
      setFace: faceTool,
      setLed: ledTool,
      setDecorator: decoratorTool,
      getBattery: getBatteryTool,
      lookThroughCamera: cameraTool,
    },
  });
}

function getAgent() {
  if (agent) return agent;
  agent = createAgent();
  return agent;
}

function getTextOnlyAgent(): any {
  if (textOnlyAgent) return textOnlyAgent;

  const model = getModel();

  textOnlyAgent = new ToolLoopAgent({
    model: model as never,
    instructions: `${systemPrompt}\n\nCRITICAL: You must speak in every response. Never respond silently or with only actions. Always include text, even if brief.`,
    stopWhen: stepCountIs(3),
    maxOutputTokens: 256,
    maxRetries: 1,
  });

  return textOnlyAgent;
}

const cameraTool = tool({
  description:
    "Capture one image from StackChan's camera and insert it into the conversation. Use this when the user asks what you see, points something out, or asks about the room or an object. Default to normal color. If the image is too dark to answer confidently, call this tool again with enhance=true.",
  inputSchema: z.object({
    enhance: z
      .boolean()
      .optional()
      .describe(
        "Use adaptive contrast/night-vision processing for dark scenes. Default false.",
      ),
  }),
  execute: async ({ enhance }) => {
    const requestId = `img-${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 8)}`;
    const imagePromise = registry.waitForImage(requestId);
    device.captureImage(requestId, enhance ?? false);
    const image = await imagePromise;

    return {
      enhanced: enhance ?? false,
      data: Buffer.from(image.data).toString("base64"),
      mediaType: image.mediaType,
      width: image.width,
      height: image.height,
    };
  },
  toModelOutput: ({ output }) => ({
    type: "content",
    value: [
      {
        type: "text",
        text: `Camera image captured${output.enhanced ? " with enhanced contrast" : " with normal color"}.`,
      },
      { type: "image-data", data: output.data, mediaType: output.mediaType },
    ],
  }),
});

function getModel() {
  const { createOpenRouter } = require("@openrouter/ai-sdk-provider");
  const openrouter = createOpenRouter({ apiKey: config.openrouterApiKey });
  return openrouter(config.openrouterModel);
}

export async function* streamStackyAgentText(
  input: string | ModelMessage[],
): AsyncGenerator<string, void, unknown> {
  const callT0 = Date.now();
  const isMessages = typeof input !== "string";
  const msgCount = isMessages ? input.length : 1;
  console.log(
    `[turn] agent call start (${isMessages ? "messages" : "prompt"}, ${msgCount} msgs)`,
  );

  try {
    const result = await getAgent().stream(
      isMessages ? { messages: input } : { prompt: input },
    );
    console.log(`[turn] agent call returned in ${Date.now() - callT0}ms`);

    let spokenChars = 0;
    let rawTotal = "";
    let chunkCount = 0;
    let firstChunkAt = 0;

    for await (const chunk of result.textStream) {
      if (!firstChunkAt) {
        firstChunkAt = Date.now();
        console.log(
          `[turn] agent ttft: ${firstChunkAt - callT0}ms, first chunk: ${JSON.stringify(chunk.slice(0, 80))}`,
        );
      }
      chunkCount++;
      rawTotal += chunk;
      const text = chunk.replace(/[*_`]/g, "").replace(/\s+/g, " ");
      if (!text) continue;

      spokenChars += text.length;
      yield text;
    }

    const totalMs = Date.now() - callT0;
    console.log(`[turn] agent raw textStream:`, JSON.stringify(rawTotal));
    console.log(
      `[turn] agent stats: ${chunkCount} chunks, ${spokenChars} spoken chars, ${totalMs}ms total`,
    );
    Promise.resolve(result.steps)
      .then((steps: unknown[]) => {
        console.log(
          `[turn] agent step count: ${steps.length}`,
        );
        for (const [i, step] of (
          steps as Array<Record<string, unknown>>
        ).entries()) {
          const usage = step.usage as
            | { promptTokens?: number; completionTokens?: number }
            | undefined;
          if (usage)
            console.log(
              `[turn]   step ${i + 1}: prompt=${usage.promptTokens ?? "?"} completion=${usage.completionTokens ?? "?"} tokens`,
            );
        }
      })
      .catch(() => {
        console.log("[turn] agent step count: unavailable");
      });

    if (spokenChars === 0 && isMessages) {
      console.log(
        `[turn] agent tool-only response after ${totalMs}ms, retrying with text-only agent`,
      );
      const retryT0 = Date.now();
      const retry = await getTextOnlyAgent().stream({ messages: input });
      console.log(`[turn] agent retry returned in ${Date.now() - retryT0}ms`);
      for await (const chunk of retry.textStream) {
        const text = chunk.replace(/[*_`]/g, "").replace(/\s+/g, " ");
        if (!text) continue;
        spokenChars += text.length;
        yield text;
      }
      console.log(
        `[turn] agent retry total: ${Date.now() - retryT0}ms, ${spokenChars} chars`,
      );
    }

    if (spokenChars === 0) {
      const fallback =
        typeof input === "string"
          ? `I heard: ${input}`
          : `I heard: ${String(input.at(-1)?.content ?? "...")}`;
      console.log("[turn] agent fallback:", JSON.stringify(fallback));
      yield fallback;
    }
  } catch (error) {
    console.error(
      `[turn] agent error after ${Date.now()}ms:`,
      error instanceof Error ? error.message : String(error),
    );
    if (registry.hasDevice()) {
      device.screen(
        error instanceof Error ? error.message : "Agent failed",
        "error",
      );
      device.led("#ff3333");
    }
    throw error;
  }
}
