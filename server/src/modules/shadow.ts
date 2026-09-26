/**
 * Shadow chat: a side channel scoped to a Twitch channel but carried entirely
 * by this server, so it keeps working while a user is banned on Twitch itself.
 *
 * Channel moderators still hold authority here. A restriction issued through
 * `restrict()` silences the user in this room exactly as a Twitch timeout would
 * silence them in the real chat.
 */

import type { Db } from "../db.ts";
import type { ShadowAuthor, ShadowMessage } from "../protocol.ts";

/** How many past messages a client receives when it joins a room. */
const HISTORY_LIMIT = 60;

/** Messages older than this are pruned on write. */
const RETENTION_MS = 24 * 60 * 60 * 1000;

export interface Restriction {
  /** Epoch millis the restriction lifts, or null when permanent. */
  until: number | null;
  reason: string;
}

interface MessageRow {
  id: number;
  channel_id: string;
  author_id: string;
  author_login: string;
  body: string;
  sent_at: number;
}

interface RestrictionRow {
  expires_at: number | null;
  reason: string;
}

export class ShadowChat {
  constructor(
    private readonly db: Db,
    private readonly maxMessageLength: number,
    private readonly now: () => number = Date.now,
  ) {}

  /**
   * Returns the restriction in force for a user, or null when they may speak.
   * Expired restrictions are deleted as they are discovered, so the table does
   * not accumulate dead rows.
   */
  restrictionFor(channelId: string, twitchId: string): Restriction | null {
    const row = this.db
      .prepare(
        `SELECT expires_at, reason FROM shadow_restrictions
         WHERE channel_id = ? AND twitch_id = ?`,
      )
      .get(channelId, twitchId) as RestrictionRow | undefined;

    if (row === undefined) {
      return null;
    }

    if (row.expires_at !== null && row.expires_at <= this.now()) {
      this.db
        .prepare(
          "DELETE FROM shadow_restrictions WHERE channel_id = ? AND twitch_id = ?",
        )
        .run(channelId, twitchId);
      return null;
    }

    return { until: row.expires_at, reason: row.reason };
  }

  /**
   * Silences a user in one room. Passing `durationMs` as null makes it
   * permanent; passing a duration replaces any existing restriction.
   */
  restrict(
    channelId: string,
    twitchId: string,
    issuedBy: string,
    durationMs: number | null,
    reason = "",
  ): Restriction {
    const now = this.now();
    const expiresAt = durationMs === null ? null : now + durationMs;

    this.db
      .prepare(
        `INSERT INTO shadow_restrictions
           (channel_id, twitch_id, expires_at, reason, issued_by, issued_at)
         VALUES (?, ?, ?, ?, ?, ?)
         ON CONFLICT (channel_id, twitch_id) DO UPDATE SET
           expires_at = excluded.expires_at,
           reason = excluded.reason,
           issued_by = excluded.issued_by,
           issued_at = excluded.issued_at`,
      )
      .run(channelId, twitchId, expiresAt, reason, issuedBy, now);

    return { until: expiresAt, reason };
  }

  /** Lifts any restriction on a user, mirroring a Twitch unban. */
  lift(channelId: string, twitchId: string): void {
    this.db
      .prepare("DELETE FROM shadow_restrictions WHERE channel_id = ? AND twitch_id = ?")
      .run(channelId, twitchId);
  }

  /**
   * Records a message. Throws {@link ShadowRejection} when the user is silenced
   * or the body is too long, so callers can map the reason onto a wire error.
   */
  post(channelId: string, author: ShadowAuthor, body: string): ShadowMessage {
    const trimmed = body.trim();
    if (trimmed.length === 0) {
      throw new ShadowRejection("bad_frame", "message was empty");
    }
    if (trimmed.length > this.maxMessageLength) {
      throw new ShadowRejection(
        "too_long",
        `message exceeds ${this.maxMessageLength} characters`,
      );
    }

    const restriction = this.restrictionFor(channelId, author.id);
    if (restriction !== null) {
      throw new ShadowRejection(
        "restricted",
        restriction.until === null
          ? "you are banned from this shadow chat"
          : "you are timed out in this shadow chat",
      );
    }

    const sentAt = this.now();
    const result = this.db
      .prepare(
        `INSERT INTO shadow_messages
           (channel_id, author_id, author_login, body, sent_at)
         VALUES (?, ?, ?, ?, ?)`,
      )
      .run(channelId, author.id, author.login, trimmed, sentAt);

    this.prune(channelId);

    return {
      id: Number(result.lastInsertRowid),
      channel: channelId,
      author,
      body: trimmed,
      sentAt,
    };
  }

  /** The most recent messages in a room, oldest first. */
  history(channelId: string, limit = HISTORY_LIMIT): ShadowMessage[] {
    const rows = this.db
      .prepare(
        `SELECT id, channel_id, author_id, author_login, body, sent_at
         FROM shadow_messages
         WHERE channel_id = ?
         ORDER BY sent_at DESC, id DESC
         LIMIT ?`,
      )
      .all(channelId, limit) as MessageRow[];

    return rows.reverse().map((row) => ({
      id: row.id,
      channel: row.channel_id,
      author: {
        id: row.author_id,
        login: row.author_login,
        displayName: row.author_login,
      },
      body: row.body,
      sentAt: row.sent_at,
    }));
  }

  private prune(channelId: string): void {
    this.db
      .prepare("DELETE FROM shadow_messages WHERE channel_id = ? AND sent_at < ?")
      .run(channelId, this.now() - RETENTION_MS);
  }
}

export class ShadowRejection extends Error {
  constructor(
    readonly code: "bad_frame" | "restricted" | "too_long",
    message: string,
  ) {
    super(message);
    this.name = "ShadowRejection";
  }
}
