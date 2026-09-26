/**
 * Moderator authorisation.
 *
 * Several actions are reserved for a channel's moderators: silencing someone in
 * a shadow room, vouching for someone in the ban registry. We must not take the
 * client's word for who moderates what, so the set is read from Twitch using the
 * caller's own token and the `user:read:moderated_channels` scope.
 *
 * Results are cached per user for a short time. Moderator status changes rarely
 * relative to how often it is checked, and a stale grant is bounded by the TTL.
 */

import { request } from "undici";

/** How long a user's moderated-channel set stays cached. */
const CACHE_TTL_MS = 2 * 60 * 1000;

/** Twitch pages this endpoint; stop after this many pages as a safety bound. */
const MAX_PAGES = 20;

export interface ModerationLookup {
  /** True when `userId` moderates `channelId`. */
  moderates(token: string, userId: string, channelId: string): Promise<boolean>;
  /** Every channel `userId` moderates. */
  moderatedChannels(token: string, userId: string): Promise<Set<string>>;
}

interface CacheEntry {
  channels: Set<string>;
  expiresAt: number;
}

export class ModerationError extends Error {
  constructor(
    message: string,
    readonly statusCode: number,
  ) {
    super(message);
    this.name = "ModerationError";
  }
}

export class TwitchModeration implements ModerationLookup {
  private readonly cache = new Map<string, CacheEntry>();

  constructor(
    private readonly clientId: string,
    private readonly now: () => number = Date.now,
  ) {}

  async moderates(
    token: string,
    userId: string,
    channelId: string,
  ): Promise<boolean> {
    // A broadcaster is always in charge of their own channel, and Twitch does
    // not list them among their own moderators.
    if (userId === channelId) {
      return true;
    }

    const channels = await this.moderatedChannels(token, userId);
    return channels.has(channelId);
  }

  async moderatedChannels(token: string, userId: string): Promise<Set<string>> {
    const cached = this.cache.get(userId);
    if (cached !== undefined && cached.expiresAt > this.now()) {
      return cached.channels;
    }
    this.cache.delete(userId);

    const channels = await this.fetchModeratedChannels(token, userId);
    this.cache.set(userId, {
      channels,
      expiresAt: this.now() + CACHE_TTL_MS,
    });

    return channels;
  }

  /** Drops the cached set, so a change takes effect without waiting out the TTL. */
  forget(userId: string): void {
    this.cache.delete(userId);
  }

  private async fetchModeratedChannels(
    token: string,
    userId: string,
  ): Promise<Set<string>> {
    const channels = new Set<string>();
    let cursor: string | undefined;

    for (let page = 0; page < MAX_PAGES; page += 1) {
      const query = new URLSearchParams({ user_id: userId, first: "100" });
      if (cursor !== undefined) {
        query.set("after", cursor);
      }

      let status: number;
      let body: unknown;
      try {
        const response = await request(
          `https://api.twitch.tv/helix/moderation/channels?${query.toString()}`,
          {
            method: "GET",
            headers: {
              authorization: `Bearer ${token}`,
              "client-id": this.clientId,
            },
          },
        );
        status = response.statusCode;
        body = await response.body.json();
      } catch {
        throw new ModerationError("could not reach Twitch", 502);
      }

      if (status === 401) {
        throw new ModerationError("Twitch rejected the token", 401);
      }
      if (status === 403) {
        throw new ModerationError(
          "the token lacks the user:read:moderated_channels scope",
          403,
        );
      }
      if (status !== 200) {
        throw new ModerationError(`unexpected Twitch status ${status}`, 502);
      }

      const page_ = parseModeratedPage(body);
      for (const id of page_.channelIds) {
        channels.add(id);
      }

      if (page_.cursor === undefined) {
        return channels;
      }
      cursor = page_.cursor;
    }

    // Hitting the page bound means something is wrong with pagination rather
    // than that the user moderates two thousand channels; return what we have.
    return channels;
  }
}

export function parseModeratedPage(body: unknown): {
  channelIds: string[];
  cursor: string | undefined;
} {
  if (typeof body !== "object" || body === null) {
    throw new ModerationError("malformed Twitch response", 502);
  }

  const payload = body as { data?: unknown; pagination?: unknown };
  if (!Array.isArray(payload.data)) {
    throw new ModerationError("Twitch response had no data array", 502);
  }

  const channelIds: string[] = [];
  for (const entry of payload.data) {
    if (typeof entry !== "object" || entry === null) {
      continue;
    }
    const id = (entry as { broadcaster_id?: unknown }).broadcaster_id;
    if (typeof id === "string" && id !== "") {
      channelIds.push(id);
    }
  }

  let cursor: string | undefined;
  if (typeof payload.pagination === "object" && payload.pagination !== null) {
    const raw = (payload.pagination as { cursor?: unknown }).cursor;
    // Twitch sends an empty pagination object on the last page.
    if (typeof raw === "string" && raw !== "") {
      cursor = raw;
    }
  }

  return { channelIds, cursor };
}
