/**
 * Runtime configuration, read once from the environment.
 *
 * Every value has a development-friendly default except the Twitch client id,
 * which must be supplied because token validation is meaningless without it.
 */

export interface Config {
  /** TCP port the HTTP + WebSocket server listens on. */
  port: number;
  /** Interface to bind. Use 0.0.0.0 behind a reverse proxy. */
  host: string;
  /** Path to the SQLite database file, or ":memory:" for tests. */
  databasePath: string;
  /** Twitch application client id, used when validating user tokens. */
  twitchClientId: string;
  /** Origins permitted to call the HTTP API. "*" disables the check. */
  allowedOrigins: string[];
  /** How long a validated Twitch token stays cached, in milliseconds. */
  tokenCacheTtlMs: number;
  /** A presence entry older than this is treated as offline. */
  presenceTimeoutMs: number;
  /** Largest shadow-chat message we accept, in UTF-16 code units. */
  maxShadowMessageLength: number;
}

function intFromEnv(name: string, fallback: number): number {
  const raw = process.env[name];
  if (raw === undefined || raw === "") {
    return fallback;
  }

  const parsed = Number.parseInt(raw, 10);
  if (!Number.isFinite(parsed) || parsed <= 0) {
    throw new Error(`${name} must be a positive integer, got ${JSON.stringify(raw)}`);
  }

  return parsed;
}

export function loadConfig(env: NodeJS.ProcessEnv = process.env): Config {
  const origins = (env.ALLOWED_ORIGINS ?? "*")
    .split(",")
    .map((value) => value.trim())
    .filter((value) => value.length > 0);

  return {
    port: intFromEnv("PORT", 8080),
    host: env.HOST ?? "127.0.0.1",
    databasePath: env.DATABASE_PATH ?? "./data/server.sqlite",
    twitchClientId: env.TWITCH_CLIENT_ID ?? "",
    allowedOrigins: origins.length > 0 ? origins : ["*"],
    tokenCacheTtlMs: intFromEnv("TOKEN_CACHE_TTL_MS", 5 * 60 * 1000),
    presenceTimeoutMs: intFromEnv("PRESENCE_TIMEOUT_MS", 90 * 1000),
    maxShadowMessageLength: intFromEnv("MAX_SHADOW_MESSAGE_LENGTH", 500),
  };
}
