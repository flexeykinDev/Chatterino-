/**
 * The record of accounts we have seen.
 *
 * Other tables reference `users` by Twitch id, so a row must exist before
 * presence or a paint selection can be stored. Keeping that in its own module
 * makes the dependency explicit: it used to be a side effect of token
 * validation, which meant anything that validated a token differently silently
 * broke referential integrity downstream.
 */

import type { Identity } from "../auth.ts";
import type { Db } from "../db.ts";

export interface UserRecord {
  twitchId: string;
  login: string;
  displayName: string;
  firstSeenAt: number;
  lastSeenAt: number;
}

interface UserRow {
  twitch_id: string;
  login: string;
  display_name: string;
  first_seen_at: number;
  last_seen_at: number;
}

export class Users {
  constructor(
    private readonly db: Db,
    private readonly now: () => number = Date.now,
  ) {}

  /**
   * Records an account, or refreshes a known one. A login or display name can
   * change on Twitch, so both are updated on every sighting, while the
   * first-seen timestamp is preserved.
   */
  remember(identity: Identity): void {
    const now = this.now();
    this.db
      .prepare(
        `INSERT INTO users (twitch_id, login, display_name, first_seen_at, last_seen_at)
         VALUES (?, ?, ?, ?, ?)
         ON CONFLICT (twitch_id) DO UPDATE SET
           login        = excluded.login,
           display_name = excluded.display_name,
           last_seen_at = excluded.last_seen_at`,
      )
      .run(identity.twitchId, identity.login, identity.displayName, now, now);
  }

  get(twitchId: string): UserRecord | null {
    const row = this.db
      .prepare(
        `SELECT twitch_id, login, display_name, first_seen_at, last_seen_at
         FROM users WHERE twitch_id = ?`,
      )
      .get(twitchId) as UserRow | undefined;

    if (row === undefined) {
      return null;
    }

    return {
      twitchId: row.twitch_id,
      login: row.login,
      displayName: row.display_name,
      firstSeenAt: row.first_seen_at,
      lastSeenAt: row.last_seen_at,
    };
  }

  /** Resolves a login to an account we have seen, for lookups by name. */
  findByLogin(login: string): UserRecord | null {
    const row = this.db
      .prepare(
        `SELECT twitch_id, login, display_name, first_seen_at, last_seen_at
         FROM users WHERE login = ?`,
      )
      .get(login.toLowerCase()) as UserRow | undefined;

    if (row === undefined) {
      return null;
    }

    return {
      twitchId: row.twitch_id,
      login: row.login,
      displayName: row.display_name,
      firstSeenAt: row.first_seen_at,
      lastSeenAt: row.last_seen_at,
    };
  }
}
