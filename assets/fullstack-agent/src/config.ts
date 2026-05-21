const numberFromEnv = (name: string, fallback: number) => {
  const raw = Bun.env[name];
  if (!raw) return fallback;
  const parsed = Number(raw);
  return Number.isFinite(parsed) ? parsed : fallback;
};

export const config = {
  host: Bun.env.STACKY_SERVER_HOST ?? "0.0.0.0",
  port: numberFromEnv("STACKY_SERVER_PORT", 6001),
  publicBaseUrl:
    Bun.env.STACKY_PUBLIC_BASE_URL ??
    `http://localhost:${numberFromEnv("STACKY_SERVER_PORT", 6001)}`,
  deviceToken: Bun.env.STACKY_DEVICE_TOKEN ?? "dev-token-change-me",
  openrouterApiKey: Bun.env.OPENROUTER_API_KEY,
  deepgramApiKey: Bun.env.DEEPGRAM_API_KEY,
  deepgramTtsModel: "aura-2-pandora-en",
  openrouterModel: "anthropic/claude-haiku-4.5:nitro",
};

export function healthConfig() {
  return {
    host: config.host,
    port: config.port,
    publicBaseUrl: config.publicBaseUrl,
    hasOpenrouterApiKey: Boolean(config.openrouterApiKey),
    hasDeepgramApiKey: Boolean(config.deepgramApiKey),
    hasDeviceToken: Boolean(config.deviceToken),
  };
}
