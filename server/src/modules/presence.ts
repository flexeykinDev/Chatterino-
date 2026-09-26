/**
 * Presence tracking.
 *
 * The client renders a three-state badge from this: lit when the account has a
 * live session, grey when it has used the client before but nothing is running
 * now, and absent when we have never seen the account at all. A user may hide
 * their own presence, in which case they always read as absent to others.
 */

import type { Db } from "../db.ts";

export type PresenceState = "online" | "offline" | "unknown";

interface PresenceRow {
  last_beat_at: number;
  hidden: number;
}

export class Presence {
  constructor(
    private readonly db: Db,
    private readonly timeoutMs: number,
    private readonly now: () => number = Date.now,
  ) {}

  /** Records that a session is alive. Called on connect and on each heartbeat. */
  beat(twitchId: string, clientVersion: string, hidden: boolean): void {
    this.db
      .prepare(
        `INSERT INTO presence (twitch_id, last_beat_at, client_version, hidden)
         VALUES (?, ?, ?, ?)
         ON CONFLICT (twitch_id) DO UPDATE SET
           last_beat_at = excluded.last_beat_at,
           client_version = excluded.client_version,
           hidden = excluded.hidden`,
      )
      .run(twitchId, this.now(), clientVersion, hidden ? 1 : 0);
  }

  /**
   * Marks a session as ended. The row is kept so the badge can still show grey:
   * "has used this client, but is not running it now".
   */
  release(twitchId: string): void {
    this.db
      .prepare(
        "UPDATE presence SET last_beat_at = ? WHERE twitch_id = ?",
      )
      .run(this.now() - this.timeoutMs - 1, twitchId);
  }

  stateOf(twitchId: string): PresenceState {
    const row = this.db
      .prepare("SELECT last_beat_at, hidden FROM presence WHERE twitch_id = ?")
      .get(twitchId) as PresenceRow | undefined;

    if (row === undefined || row.hidden === 1) {
      return "unknown";
    }

    return row.last_beat_at > this.now() - this.timeoutMs ? "online" : "offline";
  }

  /** Batch lookup, so a full chat window costs one query instead of hundreds. */
  statesOf(twitchIds: string[]): Map<string, PresenceState> {
    const states = new Map<string, PresenceState>();
    if (twitchIds.length === 0) {
      return states;
    }

    const unique = [...new Set(twitchIds)];
    for (const id of unique) {
      states.set(id, "unknown");
    }

    const placeholders = unique.map(() => "?").join(", ");
    const rows = this.db
      .prepare(
        `SELECT twitch_id, last_beat_at, hidden FROM presence
         WHERE twitch_id IN (${placeholders})`,
      )
      .all(...unique) as (PresenceRow & { twitch_id: string })[];

    const cutoff = this.now() - this.timeoutMs;
    for (const row of rows) {
      if (row.hidden === 1) {
        continue;
      }
      states.set(row.twitch_id, row.last_beat_at > cutoff ? "online" : "offline");
    }

    return states;
  }
}

/**
 * Typing indicators, held in memory only. They are worthless a few seconds after
 * the fact, so persisting them would be wasted writes.
 */
export class TypingTracker {
  /** channel id -> login -> epoch millis the entry expires. */
  private readonly rooms = new Map<string, Map<string, number>>();

  constructor(
    private readonly ttlMs = 8000,
    private readonly now: () => number = Date.now,
  ) {}

  set(channelId: string, login: string, active: boolean): void {
    if (!active) {
      this.rooms.get(channelId)?.delete(login);
      return;
    }

    let room = this.rooms.get(channelId);
    if (room === undefined) {
      room = new Map();
      this.rooms.set(channelId, room);
    }
    room.set(login, this.now() + this.ttlMs);
  }

  /** Logins currently typing in a channel, with expired entries swept out. */
  active(channelId: string): string[] {
    const room = this.rooms.get(channelId);
    if (room === undefined) {
      return [];
    }

    const now = this.now();
    for (const [login, expiresAt] of room) {
      if (expiresAt <= now) {
        room.delete(login);
      }
    }

    if (room.size === 0) {
      this.rooms.delete(channelId);
    }

    return [...room.keys()];
  }
}
