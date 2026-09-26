/**
 * Shared library of user-authored nickname paints.
 *
 * A paint is a gradient (or flat colour) applied to a username in chat. Authors
 * keep their own paints privately until they choose to share one, at which point
 * it appears in the public library for anyone to wear.
 *
 * Definitions are validated before storage. They are rendered by every client
 * that sees the wearer, so a malformed or absurd definition is not merely the
 * author's problem — it would degrade other people's chat.
 */

import { randomUUID } from "node:crypto";
import { z } from "zod";
import type { Db } from "../db.ts";

/** Upper bounds chosen so a definition stays cheap for clients to render. */
const MAX_STOPS = 12;
const MAX_NAME_LENGTH = 48;
const MAX_SHADOWS = 4;

const colourStop = z.object({
  /** Position along the gradient, 0 to 1 inclusive. */
  at: z.number().min(0).max(1),
  /** 8-digit RGBA hex, so alpha is always explicit. */
  colour: z.string().regex(/^#[0-9a-fA-F]{8}$/, "expected an #rrggbbaa colour"),
});

const shadow = z.object({
  offsetX: z.number().min(-16).max(16),
  offsetY: z.number().min(-16).max(16),
  radius: z.number().min(0).max(16),
  colour: z.string().regex(/^#[0-9a-fA-F]{8}$/, "expected an #rrggbbaa colour"),
});

export const paintDefinition = z
  .object({
    kind: z.enum(["flat", "linear", "radial"]),
    /** Gradient rotation in degrees. Ignored for flat and radial paints. */
    angle: z.number().min(0).max(360).default(0),
    stops: z.array(colourStop).min(1).max(MAX_STOPS),
    shadows: z.array(shadow).max(MAX_SHADOWS).default([]),
  })
  .superRefine((value, ctx) => {
    if (value.kind === "flat" && value.stops.length !== 1) {
      ctx.addIssue({
        code: z.ZodIssueCode.custom,
        message: "a flat paint must have exactly one stop",
        path: ["stops"],
      });
    }
    if (value.kind !== "flat" && value.stops.length < 2) {
      ctx.addIssue({
        code: z.ZodIssueCode.custom,
        message: "a gradient needs at least two stops",
        path: ["stops"],
      });
    }
  });

export type PaintDefinition = z.infer<typeof paintDefinition>;

export const paintName = z
  .string()
  .trim()
  .min(1, "a paint needs a name")
  .max(MAX_NAME_LENGTH);

export interface Paint {
  id: string;
  authorId: string;
  name: string;
  definition: PaintDefinition;
  shared: boolean;
  createdAt: number;
}

interface PaintRow {
  id: string;
  author_id: string;
  name: string;
  definition: string;
  shared: number;
  created_at: number;
}

export class PaintRejection extends Error {
  constructor(
    message: string,
    readonly statusCode: number,
  ) {
    super(message);
    this.name = "PaintRejection";
  }
}

export class Paints {
  constructor(
    private readonly db: Db,
    private readonly now: () => number = Date.now,
    private readonly newId: () => string = randomUUID,
  ) {}

  /** Validates and stores a new paint, private until explicitly shared. */
  create(authorId: string, name: unknown, definition: unknown): Paint {
    const parsedName = paintName.safeParse(name);
    if (!parsedName.success) {
      throw new PaintRejection(
        parsedName.error.issues[0]?.message ?? "invalid name",
        400,
      );
    }

    const parsedDefinition = paintDefinition.safeParse(definition);
    if (!parsedDefinition.success) {
      throw new PaintRejection(
        parsedDefinition.error.issues[0]?.message ?? "invalid definition",
        400,
      );
    }

    const paint: Paint = {
      id: this.newId(),
      authorId,
      name: parsedName.data,
      definition: parsedDefinition.data,
      shared: false,
      createdAt: this.now(),
    };

    this.db
      .prepare(
        `INSERT INTO paints (id, author_id, name, definition, shared, created_at)
         VALUES (?, ?, ?, ?, 0, ?)`,
      )
      .run(
        paint.id,
        paint.authorId,
        paint.name,
        JSON.stringify(paint.definition),
        paint.createdAt,
      );

    return paint;
  }

  get(paintId: string): Paint | null {
    const row = this.db
      .prepare(
        `SELECT id, author_id, name, definition, shared, created_at
         FROM paints WHERE id = ?`,
      )
      .get(paintId) as PaintRow | undefined;

    return row === undefined ? null : rowToPaint(row);
  }

  /** Every paint an author has made, newest first. */
  byAuthor(authorId: string): Paint[] {
    const rows = this.db
      .prepare(
        `SELECT id, author_id, name, definition, shared, created_at
         FROM paints WHERE author_id = ?
         ORDER BY created_at DESC, id ASC`,
      )
      .all(authorId) as PaintRow[];

    return rows.map(rowToPaint);
  }

  /** The public library, newest first. */
  library(limit = 100, offset = 0): Paint[] {
    const rows = this.db
      .prepare(
        `SELECT id, author_id, name, definition, shared, created_at
         FROM paints WHERE shared = 1
         ORDER BY created_at DESC, id ASC
         LIMIT ? OFFSET ?`,
      )
      .all(limit, offset) as PaintRow[];

    return rows.map(rowToPaint);
  }

  /**
   * Publishes or unpublishes a paint. Only the author may do this, and only
   * their own paints exist to be published.
   */
  setShared(paintId: string, authorId: string, shared: boolean): Paint {
    const paint = this.requireOwned(paintId, authorId);

    this.db
      .prepare("UPDATE paints SET shared = ? WHERE id = ?")
      .run(shared ? 1 : 0, paintId);

    return { ...paint, shared };
  }

  /** Renames a paint the caller authored. */
  rename(paintId: string, authorId: string, name: unknown): Paint {
    const paint = this.requireOwned(paintId, authorId);

    const parsedName = paintName.safeParse(name);
    if (!parsedName.success) {
      throw new PaintRejection(
        parsedName.error.issues[0]?.message ?? "invalid name",
        400,
      );
    }

    this.db
      .prepare("UPDATE paints SET name = ? WHERE id = ?")
      .run(parsedName.data, paintId);

    return { ...paint, name: parsedName.data };
  }

  /**
   * Deletes a paint the caller authored. Anyone wearing it falls back to their
   * default colour, because the selection column drops to NULL.
   */
  remove(paintId: string, authorId: string): void {
    this.requireOwned(paintId, authorId);
    this.db.prepare("DELETE FROM paints WHERE id = ?").run(paintId);
  }

  /**
   * Sets the paint a user wears. Passing null clears it.
   *
   * A user may wear their own paint, or any paint that has been shared. Wearing
   * someone else's unshared paint is refused: it has not been published.
   */
  wear(twitchId: string, paintId: string | null): void {
    if (paintId === null) {
      this.db
        .prepare("DELETE FROM paint_selection WHERE twitch_id = ?")
        .run(twitchId);
      return;
    }

    const paint = this.get(paintId);
    if (paint === null) {
      throw new PaintRejection("no such paint", 404);
    }
    if (!paint.shared && paint.authorId !== twitchId) {
      throw new PaintRejection("that paint has not been shared", 403);
    }

    this.db
      .prepare(
        `INSERT INTO paint_selection (twitch_id, paint_id) VALUES (?, ?)
         ON CONFLICT (twitch_id) DO UPDATE SET paint_id = excluded.paint_id`,
      )
      .run(twitchId, paintId);
  }

  /** The paint a user currently wears, if any. */
  wornBy(twitchId: string): Paint | null {
    const row = this.db
      .prepare(
        `SELECT p.id, p.author_id, p.name, p.definition, p.shared, p.created_at
         FROM paint_selection s
         JOIN paints p ON p.id = s.paint_id
         WHERE s.twitch_id = ?`,
      )
      .get(twitchId) as PaintRow | undefined;

    return row === undefined ? null : rowToPaint(row);
  }

  /**
   * Paints worn by many users at once, so a chat window resolves every visible
   * name in one query. Users wearing nothing are omitted.
   */
  wornByMany(twitchIds: string[]): Map<string, Paint> {
    const worn = new Map<string, Paint>();
    if (twitchIds.length === 0) {
      return worn;
    }

    const unique = [...new Set(twitchIds)];
    const placeholders = unique.map(() => "?").join(", ");
    const rows = this.db
      .prepare(
        `SELECT s.twitch_id, p.id, p.author_id, p.name, p.definition,
                p.shared, p.created_at
         FROM paint_selection s
         JOIN paints p ON p.id = s.paint_id
         WHERE s.twitch_id IN (${placeholders})`,
      )
      .all(...unique) as (PaintRow & { twitch_id: string })[];

    for (const row of rows) {
      worn.set(row.twitch_id, rowToPaint(row));
    }

    return worn;
  }

  private requireOwned(paintId: string, authorId: string): Paint {
    const paint = this.get(paintId);
    if (paint === null) {
      throw new PaintRejection("no such paint", 404);
    }
    if (paint.authorId !== authorId) {
      throw new PaintRejection("that paint belongs to someone else", 403);
    }
    return paint;
  }
}

function rowToPaint(row: PaintRow): Paint {
  return {
    id: row.id,
    authorId: row.author_id,
    name: row.name,
    // Stored definitions were validated on the way in, so a parse failure here
    // means the row was tampered with or written by an older schema.
    definition: paintDefinition.parse(JSON.parse(row.definition)),
    shared: row.shared === 1,
    createdAt: row.created_at,
  };
}
