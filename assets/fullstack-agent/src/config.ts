const numberFromEnv = (name: string, fallback: number) => {
  const raw = Bun.env[name];
  if (!raw) return fallback;
  const parsed = Number(raw);
  return Number.isFinite(parsed) ? parsed : fallback;
};

const booleanFromEnv = (name: string, fallback: boolean) => {
  const raw = Bun.env[name];
  if (!raw) return fallback;
  return ["1", "true", "yes", "on"].includes(raw.toLowerCase());
};

export const config = {
  host: Bun.env.STACKY_SERVER_HOST ?? "0.0.0.0",
  port: numberFromEnv("STACKY_SERVER_PORT", 6001),
  publicBaseUrl:
    Bun.env.STACKY_PUBLIC_BASE_URL ??
    `http://localhost:${numberFromEnv("STACKY_SERVER_PORT", 6001)}`,
  deviceWsUrl: Bun.env.STACKY_DEVICE_WS_URL,
  openrouterApiKey: Bun.env.OPENROUTER_API_KEY,
  deepgramApiKey: Bun.env.DEEPGRAM_API_KEY,
  deepgramTtsModel: "aura-2-pandora-en",
  openrouterModel: "anthropic/claude-haiku-4.5:nitro",
  wakeWord: {
    enabled: booleanFromEnv("STACKY_WAKE_WORD_ENABLED", true),
    phrase: Bun.env.STACKY_WAKE_WORD_PHRASE ?? Bun.env.STACKY_WAKE_WORD ?? "Stacky",
    modelId: Bun.env.STACKY_WAKE_WORD_MODEL_ID ?? "stacky",
    modelUrl: Bun.env.STACKY_WAKE_WORD_MODEL_URL,
  },
};

export function healthConfig() {
  return {
    host: config.host,
    port: config.port,
    publicBaseUrl: config.publicBaseUrl,
    hasDeviceWsUrl: Boolean(config.deviceWsUrl),
    hasOpenrouterApiKey: Boolean(config.openrouterApiKey),
    hasDeepgramApiKey: Boolean(config.deepgramApiKey),
    wakeWord: {
      enabled: config.wakeWord.enabled,
      phrase: config.wakeWord.phrase,
      modelId: config.wakeWord.modelId,
      hasModelUrl: Boolean(config.wakeWord.modelUrl),
    },
  };
}
