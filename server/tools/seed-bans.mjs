#!/usr/bin/env node
// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

/**
 * Puts made-up bans into a local registry so the client has something to draw.
 *
 * The marker only appears for someone the service has a ban recorded for, and
 * the only supported way to record one is POST /v1/bans, which requires being a
 * moderator of the channel the ban happened on. That is the right rule — without
 * it anyone could poison the registry — but it makes a fresh install impossible
 * to look at: every chatter comes back unmarked, which is indistinguishable
 * from the feature not working.
 *
 * So this writes to the database directly, bypassing that rule on purpose. It
 * refuses to touch anything but a local file for the same reason.
 *
 *   node server/tools/seed-bans.mjs --offender 123456789
 *   node server/tools/seed-bans.mjs --offender 123456789 --offender 987654321
 *
 * Point it at your own Twitch user id and your own messages will carry a
 * marker, which is the quickest end-to-end check there is.
 */

import Database from "better-sqlite3";
import { resolve } from "node:path";
import { existsSync } from "node:fs";

/** Invented channels for the bans to have happened on. */
const CHANNELS = [
    { id: "700000001", login: "somechannel", reason: "spam" },
    { id: "700000002", login: "anotherchannel", reason: "ban evasion" },
    { id: "700000003", login: "thirdchannel", reason: "" },
];

const CONTEXT = [
    "buy followers cheap www.example.invalid",
    "buy followers cheap www.example.invalid",
    "why does nobody answer me",
];

function parseArgs(argv) {
    const args = {
        offenders: [],
        database: process.env.DATABASE_PATH ?? "./data/server.sqlite",
        viewing: null,
        clear: false,
    };

    for (let i = 0; i < argv.length; i++) {
        switch (argv[i]) {
            case "--offender":
                args.offenders.push(argv[++i]);
                break;
            case "--database":
                args.database = argv[++i];
                break;
            case "--clear":
                args.clear = true;
                break;
            case "--help":
            case "-h":
                args.help = true;
                break;
            default:
                throw new Error(`unknown argument: ${argv[i]}`);
        }
    }

    return args;
}

const args = parseArgs(process.argv.slice(2));

if (args.help || (args.offenders.length === 0 && !args.clear)) {
    console.log(
        "usage: node server/tools/seed-bans.mjs --offender <twitch user id> [...]\n" +
            "       [--database ./data/server.sqlite] [--clear]\n\n" +
            "Writes invented bans straight into a local registry so the client\n" +
            "has markers to draw. Development only.",
    );
    process.exit(args.help ? 0 : 1);
}

const path = resolve(args.database);

// A registry with real bans in it is a record about real people, and this tool
// invents bans. Refusing anything but an existing local file is a thin guard,
// but it is the difference between a typo seeding your test database and a typo
// seeding a deployment.
if (!existsSync(path)) {
    console.error(
        `No database at ${path}.\n` +
            "Start the server once (npm start) so it creates and migrates one.",
    );
    process.exit(1);
}

const db = new Database(path);
db.pragma("foreign_keys = ON");

if (args.clear) {
    const removed = db
        .prepare("DELETE FROM global_bans WHERE channel_id IN (?, ?, ?)")
        .run(...CHANNELS.map((c) => c.id));
    console.log("Removed %d seeded ban(s).", removed.changes);

    if (args.offenders.length === 0) {
        process.exit(0);
    }
}

const insertBan = db.prepare(
    `INSERT INTO global_bans
       (offender_id, channel_id, channel_login, reason, banned_at,
        lifted_at, cleared_by, cleared_at)
     VALUES (?, ?, ?, ?, ?, NULL, NULL, NULL)
     ON CONFLICT (offender_id, channel_id) DO UPDATE SET
       reason = excluded.reason, banned_at = excluded.banned_at,
       lifted_at = NULL, cleared_by = NULL, cleared_at = NULL
     RETURNING id`,
);
const clearContext = db.prepare(
    "DELETE FROM global_ban_context WHERE ban_id = ?",
);
const insertContext = db.prepare(
    "INSERT INTO global_ban_context (ban_id, body, sent_at) VALUES (?, ?, ?)",
);

const now = Date.now();

const seed = db.transaction((offenders) => {
    for (const offender of offenders) {
        CHANNELS.forEach((channel, index) => {
            // Spread them over the past few weeks so the history has an order
            // worth reading rather than three identical timestamps.
            const bannedAt = now - (index + 1) * 6 * 24 * 60 * 60 * 1000;

            const { id } = insertBan.get(
                offender,
                channel.id,
                channel.login,
                channel.reason,
                bannedAt,
            );

            clearContext.run(id);
            CONTEXT.forEach((body, line) => {
                insertContext.run(id, body, bannedAt - (3 - line) * 20_000);
            });
        });
    }
});

seed(args.offenders);

for (const offender of args.offenders) {
    console.log(
        "Seeded %d ban(s) for offender %s.",
        CHANNELS.length,
        offender,
    );
}

console.log(
    "\nThe marker counts bans on channels *other* than the one being read,\n" +
        "so these show up anywhere except %s.",
    CHANNELS.map((c) => c.login).join(", "),
);
