/**
 * Cross-channel ban registry.
 *
 * When someone is banned on a channel, the ban is recorded here together with
 * what they said beforehand. Other channels can then show a marker next to that
 * person, and a moderator can open the history to see where they were banned and
 * why before deciding anything.
 *
 * Two deliberate constraints:
 *
 *  - A marker is a prompt to look, not a verdict. A moderator of a channel can
 *    vouch for someone with `clear()`, which hides the marker for everyone
 *    watching that channel.
 *  - A ban that no longer holds on Twitch should stop counting. `markLifted()`
 *    records that, and lifted bans are excluded from the marker by default.
 */

import type { Db } from "../db.ts";

export interface BanContextLine {
  body: string;
  sentAt: number;
}

export interface BanRecord {
  id: number;
  offenderId: string;
  channelId: string;
  channelLogin: string;
  reason: string;
  bannedAt: number;
  /** Set once revalidation found the ban no longer in force. */
  liftedAt: number | null;
  /** Set when a moderator of that channel vouched for the offender. */
  clearedAt: number | null;
  clearedBy: string | null;
  /** What the offender said before the ban, oldest first. */
  context: BanContextLine[];
}

export interface RecordBanInput {
  offenderId: string;
  channelId: string;
  channelLogin: string;
  reason?: string;
  /** The offender's last messages on that channel, oldest first. */
  context?: BanContextLine[];
}

interface BanRow {
  id: number;
  offender_id: string;
  channel_id: string;
  channel_login: string;
  reason: string;
  banned_at: number;
  lifted_at: number | null;
  cleared_at: number | null;
  cleared_by: string | null;
}

interface ContextRow {
  ban_id: number;
  body: string;
  sent_at: number;
}

/** How many of the offender's messages we keep per ban. */
const MAX_CONTEXT_LINES = 20;

export class GlobalBans {
  constructor(
    private readonly db: Db,
    private readonly now: () => number = Date.now,
  ) {}

  /**
   * Records a ban, or refreshes an existing one for the same channel.
   *
   * Re-banning someone clears a previous `lifted_at`, because the ban is in
   * force again, and clears a previous vouch, because the channel that vouched
   * has evidently changed its mind.
   */
  record(input: RecordBanInput): BanRecord {
    const bannedAt = this.now();
    const reason = input.reason ?? "";
    const context = (input.context ?? []).slice(-MAX_CONTEXT_LINES);

    return this.db.transaction(() => {
      const row = this.db
        .prepare(
          `INSERT INTO global_bans
             (offender_id, channel_id, channel_login, reason, banned_at,
              lifted_at, cleared_by, cleared_at)
           VALUES (?, ?, ?, ?, ?, NULL, NULL, NULL)
           ON CONFLICT (offender_id, channel_id) DO UPDATE SET
             channel_login = excluded.channel_login,
             reason        = excluded.reason,
             banned_at     = excluded.banned_at,
             lifted_at     = NULL,
             cleared_by    = NULL,
             cleared_at    = NULL
           RETURNING id`,
        )
        .get(
          input.offenderId,
          input.channelId,
          input.channelLogin,
          reason,
          bannedAt,
        ) as { id: number };

      // Replace rather than append: this is the context for the current ban.
      this.db.prepare("DELETE FROM global_ban_context WHERE ban_id = ?").run(row.id);

      const insertLine = this.db.prepare(
        "INSERT INTO global_ban_context (ban_id, body, sent_at) VALUES (?, ?, ?)",
      );
      for (const line of context) {
        insertLine.run(row.id, line.body, line.sentAt);
      }

      return {
        id: row.id,
        offenderId: input.offenderId,
        channelId: input.channelId,
        channelLogin: input.channelLogin,
        reason,
        bannedAt,
        liftedAt: null,
        clearedAt: null,
        clearedBy: null,
        context,
      };
    })();
  }

  /**
   * Notes that a ban is no longer in force. The row is kept so the history still
   * shows it happened; it simply stops contributing to the marker.
   */
  markLifted(offenderId: string, channelId: string): boolean {
    const result = this.db
      .prepare(
        `UPDATE global_bans SET lifted_at = ?
         WHERE offender_id = ? AND channel_id = ? AND lifted_at IS NULL`,
      )
      .run(this.now(), offenderId, channelId);

    return result.changes > 0;
  }

  /** Reverses {@link markLifted}, for a revalidation pass that was wrong. */
  markActive(offenderId: string, channelId: string): boolean {
    const result = this.db
      .prepare(
        `UPDATE global_bans SET lifted_at = NULL
         WHERE offender_id = ? AND channel_id = ?`,
      )
      .run(offenderId, channelId);

    return result.changes > 0;
  }

  /**
   * A moderator of `channelId` vouches for the offender, hiding the marker for
   * everyone watching that channel. `clearedBy` is the moderator's Twitch id,
   * kept so the decision is attributable.
   */
  clear(offenderId: string, channelId: string, clearedBy: string): boolean {
    const result = this.db
      .prepare(
        `UPDATE global_bans SET cleared_by = ?, cleared_at = ?
         WHERE offender_id = ? AND channel_id = ? AND cleared_at IS NULL`,
      )
      .run(clearedBy, this.now(), offenderId, channelId);

    return result.changes > 0;
  }

  /** Withdraws a vouch. */
  unclear(offenderId: string, channelId: string): boolean {
    const result = this.db
      .prepare(
        `UPDATE global_bans SET cleared_by = NULL, cleared_at = NULL
         WHERE offender_id = ? AND channel_id = ?`,
      )
      .run(offenderId, channelId);

    return result.changes > 0;
  }

  /**
   * Every recorded ban for an offender, newest first, including lifted and
   * vouched-for ones. This is what the history window shows.
   */
  historyFor(offenderId: string): BanRecord[] {
    const rows = this.db
      .prepare(
        `SELECT id, offender_id, channel_id, channel_login, reason, banned_at,
                lifted_at, cleared_at, cleared_by
         FROM global_bans
         WHERE offender_id = ?
         ORDER BY banned_at DESC, id DESC`,
      )
      .all(offenderId) as BanRow[];

    if (rows.length === 0) {
      return [];
    }

    const context = this.contextFor(rows.map((row) => row.id));

    return rows.map((row) => ({
      id: row.id,
      offenderId: row.offender_id,
      channelId: row.channel_id,
      channelLogin: row.channel_login,
      reason: row.reason,
      bannedAt: row.banned_at,
      liftedAt: row.lifted_at,
      clearedAt: row.cleared_at,
      clearedBy: row.cleared_by,
      context: context.get(row.id) ?? [],
    }));
  }

  /**
   * How many bans should make a marker appear next to this person, as seen from
   * `viewingChannelId`.
   *
   * Lifted bans never count. A ban vouched for by the channel being viewed does
   * not count there, but still counts elsewhere: one channel's decision to
   * forgive someone is not binding on another. A ban on the viewing channel
   * itself does not count either, since that chat can already see it.
   */
  markerCount(offenderId: string, viewingChannelId: string): number {
    const row = this.db
      .prepare(
        `SELECT COUNT(*) AS count FROM global_bans
         WHERE offender_id = ?
           AND lifted_at IS NULL
           AND channel_id <> ?
           AND cleared_at IS NULL`,
      )
      .get(offenderId, viewingChannelId) as { count: number };

    return row.count;
  }

  /**
   * Marker counts for many offenders at once, so rendering a chat window costs
   * one query. Offenders with no marker are omitted.
   */
  markerCounts(
    offenderIds: string[],
    viewingChannelId: string,
  ): Map<string, number> {
    const counts = new Map<string, number>();
    if (offenderIds.length === 0) {
      return counts;
    }

    const unique = [...new Set(offenderIds)];
    const placeholders = unique.map(() => "?").join(", ");
    const rows = this.db
      .prepare(
        `SELECT offender_id, COUNT(*) AS count FROM global_bans
         WHERE offender_id IN (${placeholders})
           AND lifted_at IS NULL
           AND channel_id <> ?
           AND cleared_at IS NULL
         GROUP BY offender_id`,
      )
      .all(...unique, viewingChannelId) as {
      offender_id: string;
      count: number;
    }[];

    for (const row of rows) {
      counts.set(row.offender_id, row.count);
    }

    return counts;
  }

  /**
   * Channels where this offender is still banned, for the relay prompt: the
   * client intersects this with the channels the viewer moderates to offer
   * "ban them here too".
   */
  activeChannelsFor(offenderId: string): string[] {
    const rows = this.db
      .prepare(
        `SELECT channel_id FROM global_bans
         WHERE offender_id = ? AND lifted_at IS NULL
         ORDER BY banned_at DESC`,
      )
      .all(offenderId) as { channel_id: string }[];

    return rows.map((row) => row.channel_id);
  }

  /**
   * Bans not revalidated since `before`, oldest first, so a background pass can
   * re-check the ones most likely to be stale.
   */
  staleBans(before: number, limit = 100): { offenderId: string; channelId: string }[] {
    const rows = this.db
      .prepare(
        `SELECT offender_id, channel_id FROM global_bans
         WHERE lifted_at IS NULL AND banned_at < ?
         ORDER BY banned_at ASC
         LIMIT ?`,
      )
      .all(before, limit) as { offender_id: string; channel_id: string }[];

    return rows.map((row) => ({
      offenderId: row.offender_id,
      channelId: row.channel_id,
    }));
  }

  private contextFor(banIds: number[]): Map<number, BanContextLine[]> {
    const byBan = new Map<number, BanContextLine[]>();
    if (banIds.length === 0) {
      return byBan;
    }

    const placeholders = banIds.map(() => "?").join(", ");
    const rows = this.db
      .prepare(
        `SELECT ban_id, body, sent_at FROM global_ban_context
         WHERE ban_id IN (${placeholders})
         ORDER BY sent_at ASC, rowid ASC`,
      )
      .all(...banIds) as ContextRow[];

    for (const row of rows) {
      let lines = byBan.get(row.ban_id);
      if (lines === undefined) {
        lines = [];
        byBan.set(row.ban_id, lines);
      }
      lines.push({ body: row.body, sentAt: row.sent_at });
    }

    return byBan;
  }
}
