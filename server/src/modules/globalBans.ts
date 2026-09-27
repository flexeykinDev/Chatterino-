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
   * force again. It also withdraws that channel's own vouch, if it had one:
   * a channel that vouched for somebody and then banned them has plainly
   * changed its mind. Other channels' vouches are untouched, because their
   * opinion is theirs.
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
              lifted_at)
           VALUES (?, ?, ?, ?, ?, NULL)
           ON CONFLICT (offender_id, channel_id) DO UPDATE SET
             channel_login = excluded.channel_login,
             reason        = excluded.reason,
             banned_at     = excluded.banned_at,
             lifted_at     = NULL
           RETURNING id`,
        )
        .get(
          input.offenderId,
          input.channelId,
          input.channelLogin,
          reason,
          bannedAt,
        ) as { id: number };

      this.db
        .prepare(
          "DELETE FROM ban_vouches WHERE offender_id = ? AND channel_id = ?",
        )
        .run(input.offenderId, input.channelId);

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
   * A moderator of `channelId` vouches for the offender: that chat has decided
   * it trusts them despite their record elsewhere, so no marker is shown there.
   *
   * The vouch belongs to the channel that granted it and to nobody else. One
   * chat forgiving somebody says nothing about whether another should, and a
   * moderator should not be able to hide a record from chats they have no
   * standing in.
   *
   * Idempotent: vouching twice leaves one vouch, with the later moderator
   * recorded, since they are the one who most recently stood behind it.
   */
  vouch(offenderId: string, channelId: string, moderatorId: string): boolean {
    const result = this.db
      .prepare(
        `INSERT INTO ban_vouches
           (offender_id, channel_id, moderator_id, created_at)
         VALUES (?, ?, ?, ?)
         ON CONFLICT (offender_id, channel_id) DO UPDATE SET
           moderator_id = excluded.moderator_id,
           created_at   = excluded.created_at`,
      )
      .run(offenderId, channelId, moderatorId, this.now());

    return result.changes > 0;
  }

  /** Withdraws a vouch, so the marker returns for that channel. */
  withdrawVouch(offenderId: string, channelId: string): boolean {
    const result = this.db
      .prepare(
        "DELETE FROM ban_vouches WHERE offender_id = ? AND channel_id = ?",
      )
      .run(offenderId, channelId);

    return result.changes > 0;
  }

  /**
   * Which moderator granted this channel's vouch, or null when there is none.
   * Kept so the decision is attributable: a vouch is somebody's judgement, and
   * the chat should be able to see whose.
   */
  vouchedBy(offenderId: string, channelId: string): string | null {
    const row = this.db
      .prepare(
        `SELECT moderator_id FROM ban_vouches
         WHERE offender_id = ? AND channel_id = ?`,
      )
      .get(offenderId, channelId) as { moderator_id: string } | undefined;

    return row?.moderator_id ?? null;
  }

  /** Whether `channelId` has vouched for this offender. */
  isVouched(offenderId: string, channelId: string): boolean {
    return (
      this.db
        .prepare(
          "SELECT 1 FROM ban_vouches WHERE offender_id = ? AND channel_id = ?",
        )
        .get(offenderId, channelId) !== undefined
    );
  }

  /**
   * Every recorded ban for an offender, newest first, including lifted and
   * vouched-for ones. This is what the history window shows.
   */
  historyFor(offenderId: string): BanRecord[] {
    const rows = this.db
      .prepare(
        `SELECT id, offender_id, channel_id, channel_login, reason, banned_at,
                lifted_at
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
      context: context.get(row.id) ?? [],
    }));
  }

  /**
   * How many bans should make a marker appear next to this person, as seen from
   * `viewingChannelId`.
   *
   * Lifted bans never count, and neither does a ban on the viewing channel
   * itself, since that chat can already see it.
   *
   * A vouch from the viewing channel suppresses the marker there entirely: it
   * means "we trust this person here", which is about the person rather than
   * about any one ban. It changes nothing for anyone else.
   */
  markerCount(offenderId: string, viewingChannelId: string): number {
    if (this.isVouched(offenderId, viewingChannelId)) {
      return 0;
    }

    const row = this.db
      .prepare(
        `SELECT COUNT(*) AS count FROM global_bans
         WHERE offender_id = ?
           AND lifted_at IS NULL
           AND channel_id <> ?`,
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
    // The vouch check is part of the same statement rather than a second
    // query: rendering a chat window is one request, and it should stay one
    // round trip to the database too.
    const rows = this.db
      .prepare(
        `SELECT offender_id, COUNT(*) AS count FROM global_bans
         WHERE offender_id IN (${placeholders})
           AND lifted_at IS NULL
           AND channel_id <> ?
           AND offender_id NOT IN (
             SELECT offender_id FROM ban_vouches WHERE channel_id = ?
           )
         GROUP BY offender_id`,
      )
      .all(...unique, viewingChannelId, viewingChannelId) as {
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
