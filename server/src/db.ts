/**
 * SQLite storage layer.
 *
 * The schema is applied with a small forward-only migration list. Each entry
 * runs exactly once, inside a transaction, and the applied version is recorded
 * in `schema_version` so restarts are idempotent.
 */

import Database from "better-sqlite3";
import { mkdirSync } from "node:fs";
import { dirname } from "node:path";

export type Db = Database.Database;

interface Migration {
  version: number;
  name: string;
  sql: string;
}

const MIGRATIONS: Migration[] = [
  {
    version: 1,
    name: "initial",
    sql: `
      -- One row per Twitch account we have ever seen authenticate.
      CREATE TABLE users (
        twitch_id     TEXT PRIMARY KEY,
        login         TEXT NOT NULL,
        display_name  TEXT NOT NULL,
        first_seen_at INTEGER NOT NULL,
        last_seen_at  INTEGER NOT NULL
      );
      CREATE INDEX users_login_idx ON users (login);

      -- Presence powers the client badge: lit when a session is live, grey once
      -- every session has expired, absent when the user has no row at all.
      CREATE TABLE presence (
        twitch_id      TEXT PRIMARY KEY REFERENCES users (twitch_id) ON DELETE CASCADE,
        last_beat_at   INTEGER NOT NULL,
        client_version TEXT NOT NULL,
        hidden         INTEGER NOT NULL DEFAULT 0
      );
      CREATE INDEX presence_last_beat_idx ON presence (last_beat_at);

      -- Shadow chat messages, retained per channel for backfill on join.
      CREATE TABLE shadow_messages (
        id           INTEGER PRIMARY KEY AUTOINCREMENT,
        channel_id   TEXT NOT NULL,
        author_id    TEXT NOT NULL,
        author_login TEXT NOT NULL,
        body         TEXT NOT NULL,
        sent_at      INTEGER NOT NULL
      );
      CREATE INDEX shadow_messages_channel_idx
        ON shadow_messages (channel_id, sent_at DESC);

      -- Moderator-issued restrictions scoped to one channel's shadow chat.
      -- expires_at NULL means a permanent ban.
      CREATE TABLE shadow_restrictions (
        channel_id  TEXT NOT NULL,
        twitch_id   TEXT NOT NULL,
        expires_at  INTEGER,
        reason      TEXT NOT NULL DEFAULT '',
        issued_by   TEXT NOT NULL,
        issued_at   INTEGER NOT NULL,
        PRIMARY KEY (channel_id, twitch_id)
      );

      -- Cross-channel ban registry. One row per (offender, channel) pair.
      CREATE TABLE global_bans (
        id            INTEGER PRIMARY KEY AUTOINCREMENT,
        offender_id   TEXT NOT NULL,
        channel_id    TEXT NOT NULL,
        channel_login TEXT NOT NULL,
        reason        TEXT NOT NULL DEFAULT '',
        banned_at     INTEGER NOT NULL,
        -- Set once a revalidation pass finds the ban no longer in force.
        lifted_at     INTEGER,
        -- A channel moderator can vouch for someone, hiding the marker for
        -- everyone watching that channel.
        cleared_by    TEXT,
        cleared_at    INTEGER,
        UNIQUE (offender_id, channel_id)
      );
      CREATE INDEX global_bans_offender_idx ON global_bans (offender_id);

      -- The offender's last words before each ban, shown in the ban history.
      CREATE TABLE global_ban_context (
        ban_id     INTEGER NOT NULL REFERENCES global_bans (id) ON DELETE CASCADE,
        body       TEXT NOT NULL,
        sent_at    INTEGER NOT NULL
      );
      CREATE INDEX global_ban_context_ban_idx ON global_ban_context (ban_id);

      -- Shared library of user-authored nickname paints.
      CREATE TABLE paints (
        id          TEXT PRIMARY KEY,
        author_id   TEXT NOT NULL REFERENCES users (twitch_id) ON DELETE CASCADE,
        name        TEXT NOT NULL,
        -- Serialised paint definition (stops, angle, blend mode, shadows).
        definition  TEXT NOT NULL,
        shared      INTEGER NOT NULL DEFAULT 0,
        created_at  INTEGER NOT NULL
      );
      CREATE INDEX paints_author_idx ON paints (author_id);
      CREATE INDEX paints_shared_idx ON paints (shared, created_at DESC);

      -- Which paint each user currently wears.
      CREATE TABLE paint_selection (
        twitch_id TEXT PRIMARY KEY REFERENCES users (twitch_id) ON DELETE CASCADE,
        paint_id  TEXT REFERENCES paints (id) ON DELETE SET NULL
      );

      CREATE TABLE schema_version (
        version    INTEGER PRIMARY KEY,
        name       TEXT NOT NULL,
        applied_at INTEGER NOT NULL
      );
    `,
  },
  {
    version: 2,
    name: "vouches belong to the channel that grants them",
    sql: `
      -- A channel deciding it trusts somebody despite their record elsewhere.
      --
      -- This used to live on the ban row, as cleared_by/cleared_at, which made
      -- a vouch a property of the ban: one channel forgiving somebody hid that
      -- ban from every other channel too. That is a power no single moderator
      -- should have over everyone else's chat, and it contradicted what the
      -- marker claimed to mean.
      --
      -- Keyed by the *viewing* channel, so it reads "this chat trusts this
      -- person", and it says nothing about anyone else's.
      CREATE TABLE ban_vouches (
        offender_id  TEXT NOT NULL,
        channel_id   TEXT NOT NULL,
        moderator_id TEXT NOT NULL,
        created_at   INTEGER NOT NULL,
        PRIMARY KEY (offender_id, channel_id)
      );
      CREATE INDEX ban_vouches_channel_idx ON ban_vouches (channel_id);

      -- Carry over what the old columns meant as best we can. A cleared ban
      -- recorded that the channel it happened on had second thoughts, so that
      -- becomes a vouch by that same channel.
      INSERT OR IGNORE INTO ban_vouches
        (offender_id, channel_id, moderator_id, created_at)
      SELECT offender_id, channel_id, COALESCE(cleared_by, ''), cleared_at
      FROM global_bans
      WHERE cleared_at IS NOT NULL;

      ALTER TABLE global_bans DROP COLUMN cleared_by;
      ALTER TABLE global_bans DROP COLUMN cleared_at;
    `,
  },
];

export function openDatabase(path: string): Db {
  if (path !== ":memory:") {
    mkdirSync(dirname(path), { recursive: true });
  }

  const db = new Database(path);
  db.pragma("journal_mode = WAL");
  db.pragma("foreign_keys = ON");
  db.pragma("busy_timeout = 5000");

  migrate(db);
  return db;
}

function migrate(db: Db): void {
  // The very first migration creates schema_version itself, so probe for it
  // rather than querying it blindly.
  const hasVersionTable =
    db
      .prepare(
        "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = 'schema_version'",
      )
      .get() !== undefined;

  const applied = new Set<number>();
  if (hasVersionTable) {
    const rows = db.prepare("SELECT version FROM schema_version").all() as {
      version: number;
    }[];
    for (const row of rows) {
      applied.add(row.version);
    }
  }

  for (const migration of MIGRATIONS) {
    if (applied.has(migration.version)) {
      continue;
    }

    db.transaction(() => {
      db.exec(migration.sql);
      db.prepare(
        "INSERT INTO schema_version (version, name, applied_at) VALUES (?, ?, ?)",
      ).run(migration.version, migration.name, Date.now());
    })();
  }
}
