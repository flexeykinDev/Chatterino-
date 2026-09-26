/**
 * Identity verification.
 *
 * Clients never send us a password. They send the OAuth token they already hold
 * for Twitch, and we ask Twitch who it belongs to. Results are cached briefly so
 * a reconnect storm does not turn into a validation storm.
 */

import { request } from "undici";
import type { Config } from "./config.ts";
import type { Db } from "./db.ts";

export interface Identity {
  twitchId: string;
  login: string;
  displayName: string;
  /** Scopes Twitch reports for this token. */
  scopes: string[];
}

export class AuthError extends Error {
  constructor(
    message: string,
    readonly statusCode: number,
  ) {
    super(message);
    this.name = "AuthError";
  }
}

interface CacheEntry {
  identity: Identity;
  expiresAt: number;
}

/** Shape of the parts of Twitch's /validate response we rely on. */
interface ValidateResponse {
  client_id?: unknown;
  login?: unknown;
  user_id?: unknown;
  scopes?: unknown;
}

export interface Validator {
  /** Resolves the identity behind a bearer token, or throws AuthError. */
  validate(token: string): Promise<Identity>;
}

export class TwitchValidator implements Validator {
  private readonly cache = new Map<string, CacheEntry>();

  constructor(
    private readonly config: Config,
    private readonly db: Db,
    private readonly now: () => number = Date.now,
  ) {}

  async validate(token: string): Promise<Identity> {
    const normalised = normaliseToken(token);

    const cached = this.cache.get(normalised);
    if (cached !== undefined && cached.expiresAt > this.now()) {
      return cached.identity;
    }
    // Drop a stale entry eagerly so a failing revalidation cannot serve it.
    this.cache.delete(normalised);

    const identity = await this.fetchIdentity(normalised);

    this.cache.set(normalised, {
      identity,
      expiresAt: this.now() + this.config.tokenCacheTtlMs,
    });
    this.rememberUser(identity);

    return identity;
  }

  private async fetchIdentity(token: string): Promise<Identity> {
    let status: number;
    let body: unknown;

    try {
      const response = await request("https://id.twitch.tv/oauth2/validate", {
        method: "GET",
        headers: { authorization: `OAuth ${token}` },
      });
      status = response.statusCode;
      body = await response.body.json();
    } catch (cause) {
      throw new AuthError("could not reach Twitch to validate the token", 502);
    }

    if (status === 401) {
      throw new AuthError("token rejected by Twitch", 401);
    }
    if (status !== 200) {
      throw new AuthError(`unexpected validation status ${status}`, 502);
    }

    return parseIdentity(body, this.config.twitchClientId);
  }

  /**
   * Keeps a local record of the account so later joins can render a login and
   * display name without another round trip to Twitch.
   */
  private rememberUser(identity: Identity): void {
    const now = this.now();
    this.db
      .prepare(
        `INSERT INTO users (twitch_id, login, display_name, first_seen_at, last_seen_at)
         VALUES (?, ?, ?, ?, ?)
         ON CONFLICT (twitch_id) DO UPDATE SET
           login = excluded.login,
           display_name = excluded.display_name,
           last_seen_at = excluded.last_seen_at`,
      )
      .run(identity.twitchId, identity.login, identity.displayName, now, now);
  }
}

export function normaliseToken(token: string): string {
  return token.replace(/^(OAuth|Bearer)\s+/i, "").trim();
}

export function parseIdentity(body: unknown, expectedClientId: string): Identity {
  if (typeof body !== "object" || body === null) {
    throw new AuthError("malformed validation response", 502);
  }

  const payload = body as ValidateResponse;
  const userId = payload.user_id;
  const login = payload.login;

  if (typeof userId !== "string" || userId === "") {
    throw new AuthError("validation response had no user id", 502);
  }
  if (typeof login !== "string" || login === "") {
    throw new AuthError("validation response had no login", 502);
  }

  // A token minted for a different application must not grant access here: it
  // would let any unrelated app's users act as ours.
  if (expectedClientId !== "" && payload.client_id !== expectedClientId) {
    throw new AuthError("token was issued to a different application", 403);
  }

  const scopes = Array.isArray(payload.scopes)
    ? payload.scopes.filter((scope): scope is string => typeof scope === "string")
    : [];

  return {
    twitchId: userId,
    login,
    // Twitch's /validate does not return a display name, so fall back to the
    // login until a richer lookup fills it in.
    displayName: login,
    scopes,
  };
}
