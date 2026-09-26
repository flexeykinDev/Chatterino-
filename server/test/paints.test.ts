import assert from "node:assert/strict";
import { beforeEach, describe, it } from "node:test";
import { openDatabase, type Db } from "../src/db.ts";
import { PaintRejection, Paints } from "../src/modules/paints.ts";

const AUTHOR = "111";
const OTHER = "222";

function seedUser(db: Db, twitchId: string): void {
  db.prepare(
    `INSERT INTO users (twitch_id, login, display_name, first_seen_at, last_seen_at)
     VALUES (?, ?, ?, 0, 0)`,
  ).run(twitchId, `user${twitchId}`, `user${twitchId}`);
}

const gradient = {
  kind: "linear" as const,
  angle: 90,
  stops: [
    { at: 0, colour: "#ff0000ff" },
    { at: 1, colour: "#0000ffff" },
  ],
  shadows: [],
};

const flat = {
  kind: "flat" as const,
  angle: 0,
  stops: [{ at: 0, colour: "#00ff00ff" }],
  shadows: [],
};

describe("Paints", () => {
  let db: Db;
  let clock: number;
  let counter: number;
  let paints: Paints;

  beforeEach(() => {
    db = openDatabase(":memory:");
    clock = 1_700_000_000_000;
    counter = 0;
    paints = new Paints(
      db,
      () => clock,
      () => `paint-${(counter += 1)}`,
    );
    seedUser(db, AUTHOR);
    seedUser(db, OTHER);
  });

  describe("creation", () => {
    it("stores a gradient paint, private by default", () => {
      const paint = paints.create(AUTHOR, "Sunset", gradient);

      assert.equal(paint.name, "Sunset");
      assert.equal(paint.authorId, AUTHOR);
      assert.equal(paint.shared, false);
      assert.equal(paint.definition.stops.length, 2);
    });

    it("stores a flat paint", () => {
      const paint = paints.create(AUTHOR, "Lime", flat);
      assert.equal(paint.definition.kind, "flat");
    });

    it("round-trips the definition through storage", () => {
      const created = paints.create(AUTHOR, "Sunset", gradient);
      const loaded = paints.get(created.id);

      assert.deepEqual(loaded?.definition, created.definition);
    });

    it("trims the name", () => {
      assert.equal(paints.create(AUTHOR, "  Spaced  ", flat).name, "Spaced");
    });

    it("rejects an empty name", () => {
      assert.throws(() => paints.create(AUTHOR, "   ", flat), PaintRejection);
    });

    it("rejects an overlong name", () => {
      assert.throws(() => paints.create(AUTHOR, "x".repeat(49), flat), PaintRejection);
    });

    it("rejects a colour that is not #rrggbbaa", () => {
      assert.throws(
        () =>
          paints.create(AUTHOR, "Bad", {
            ...flat,
            stops: [{ at: 0, colour: "#fff" }],
          }),
        PaintRejection,
      );
    });

    it("rejects a stop outside the 0..1 range", () => {
      assert.throws(
        () =>
          paints.create(AUTHOR, "Bad", {
            ...gradient,
            stops: [
              { at: 0, colour: "#ff0000ff" },
              { at: 1.5, colour: "#0000ffff" },
            ],
          }),
        PaintRejection,
      );
    });

    it("rejects a gradient with only one stop", () => {
      assert.throws(
        () =>
          paints.create(AUTHOR, "Bad", {
            ...gradient,
            stops: [{ at: 0, colour: "#ff0000ff" }],
          }),
        PaintRejection,
      );
    });

    it("rejects a flat paint with several stops", () => {
      assert.throws(
        () => paints.create(AUTHOR, "Bad", { ...flat, stops: gradient.stops }),
        PaintRejection,
      );
    });

    it("rejects more stops than the render budget allows", () => {
      const tooMany = Array.from({ length: 13 }, (_, index) => ({
        at: index / 12,
        colour: "#ffffffff",
      }));

      assert.throws(
        () => paints.create(AUTHOR, "Bad", { ...gradient, stops: tooMany }),
        PaintRejection,
      );
    });

    it("rejects a definition that is not an object", () => {
      assert.throws(() => paints.create(AUTHOR, "Bad", "nope"), PaintRejection);
    });

    it("defaults the shadow list when omitted", () => {
      const paint = paints.create(AUTHOR, "Plain", {
        kind: "flat",
        stops: [{ at: 0, colour: "#00ff00ff" }],
      });

      assert.deepEqual(paint.definition.shadows, []);
      assert.equal(paint.definition.angle, 0);
    });

    it("rejects more shadows than allowed", () => {
      const shadows = Array.from({ length: 5 }, () => ({
        offsetX: 1,
        offsetY: 1,
        radius: 1,
        colour: "#000000ff",
      }));

      assert.throws(
        () => paints.create(AUTHOR, "Bad", { ...flat, shadows }),
        PaintRejection,
      );
    });
  });

  describe("ownership", () => {
    it("lists an author's paints newest first", () => {
      paints.create(AUTHOR, "First", flat);
      clock += 1000;
      paints.create(AUTHOR, "Second", flat);

      assert.deepEqual(
        paints.byAuthor(AUTHOR).map((paint) => paint.name),
        ["Second", "First"],
      );
    });

    it("does not list another author's paints", () => {
      paints.create(AUTHOR, "Mine", flat);
      assert.deepEqual(paints.byAuthor(OTHER), []);
    });

    it("refuses to rename someone else's paint", () => {
      const paint = paints.create(AUTHOR, "Mine", flat);

      assert.throws(
        () => paints.rename(paint.id, OTHER, "Stolen"),
        (error: unknown) =>
          error instanceof PaintRejection && error.statusCode === 403,
      );
    });

    it("refuses to delete someone else's paint", () => {
      const paint = paints.create(AUTHOR, "Mine", flat);

      assert.throws(
        () => paints.remove(paint.id, OTHER),
        (error: unknown) =>
          error instanceof PaintRejection && error.statusCode === 403,
      );
    });

    it("reports a missing paint as not found", () => {
      assert.throws(
        () => paints.rename("no-such-paint", AUTHOR, "Name"),
        (error: unknown) =>
          error instanceof PaintRejection && error.statusCode === 404,
      );
    });

    it("renames a paint the author owns", () => {
      const paint = paints.create(AUTHOR, "Before", flat);
      const renamed = paints.rename(paint.id, AUTHOR, "After");

      assert.equal(renamed.name, "After");
      assert.equal(paints.get(paint.id)?.name, "After");
    });

    it("deletes a paint the author owns", () => {
      const paint = paints.create(AUTHOR, "Doomed", flat);
      paints.remove(paint.id, AUTHOR);

      assert.equal(paints.get(paint.id), null);
    });
  });

  describe("the shared library", () => {
    it("starts empty", () => {
      paints.create(AUTHOR, "Private", flat);
      assert.deepEqual(paints.library(), []);
    });

    it("includes a paint once shared", () => {
      const paint = paints.create(AUTHOR, "Public", flat);
      paints.setShared(paint.id, AUTHOR, true);

      assert.deepEqual(
        paints.library().map((entry) => entry.name),
        ["Public"],
      );
    });

    it("drops a paint when unshared", () => {
      const paint = paints.create(AUTHOR, "Public", flat);
      paints.setShared(paint.id, AUTHOR, true);
      paints.setShared(paint.id, AUTHOR, false);

      assert.deepEqual(paints.library(), []);
    });

    it("refuses to share someone else's paint", () => {
      const paint = paints.create(AUTHOR, "Mine", flat);

      assert.throws(
        () => paints.setShared(paint.id, OTHER, true),
        (error: unknown) =>
          error instanceof PaintRejection && error.statusCode === 403,
      );
    });

    it("orders the library newest first and paginates", () => {
      for (const name of ["One", "Two", "Three"]) {
        const paint = paints.create(AUTHOR, name, flat);
        paints.setShared(paint.id, AUTHOR, true);
        clock += 1000;
      }

      assert.deepEqual(
        paints.library(2).map((entry) => entry.name),
        ["Three", "Two"],
      );
      assert.deepEqual(
        paints.library(2, 2).map((entry) => entry.name),
        ["One"],
      );
    });
  });

  describe("wearing a paint", () => {
    it("reports nothing worn by default", () => {
      assert.equal(paints.wornBy(AUTHOR), null);
    });

    it("lets an author wear their own unshared paint", () => {
      const paint = paints.create(AUTHOR, "Mine", flat);
      paints.wear(AUTHOR, paint.id);

      assert.equal(paints.wornBy(AUTHOR)?.id, paint.id);
    });

    it("refuses to wear someone else's unshared paint", () => {
      const paint = paints.create(AUTHOR, "Mine", flat);

      assert.throws(
        () => paints.wear(OTHER, paint.id),
        (error: unknown) =>
          error instanceof PaintRejection && error.statusCode === 403,
      );
    });

    it("lets anyone wear a shared paint", () => {
      const paint = paints.create(AUTHOR, "Public", flat);
      paints.setShared(paint.id, AUTHOR, true);

      paints.wear(OTHER, paint.id);
      assert.equal(paints.wornBy(OTHER)?.id, paint.id);
    });

    it("refuses to wear a paint that does not exist", () => {
      assert.throws(
        () => paints.wear(AUTHOR, "no-such-paint"),
        (error: unknown) =>
          error instanceof PaintRejection && error.statusCode === 404,
      );
    });

    it("replaces the previous selection", () => {
      const first = paints.create(AUTHOR, "First", flat);
      const second = paints.create(AUTHOR, "Second", flat);

      paints.wear(AUTHOR, first.id);
      paints.wear(AUTHOR, second.id);

      assert.equal(paints.wornBy(AUTHOR)?.id, second.id);
    });

    it("clears the selection when passed null", () => {
      const paint = paints.create(AUTHOR, "Mine", flat);
      paints.wear(AUTHOR, paint.id);
      paints.wear(AUTHOR, null);

      assert.equal(paints.wornBy(AUTHOR), null);
    });

    it("falls back to no paint when the worn paint is deleted", () => {
      const paint = paints.create(AUTHOR, "Doomed", flat);
      paints.wear(AUTHOR, paint.id);
      paints.remove(paint.id, AUTHOR);

      assert.equal(paints.wornBy(AUTHOR), null);
    });

    describe("batch lookup", () => {
      it("omits users wearing nothing", () => {
        const paint = paints.create(AUTHOR, "Mine", flat);
        paints.wear(AUTHOR, paint.id);

        const worn = paints.wornByMany([AUTHOR, OTHER]);

        assert.equal(worn.get(AUTHOR)?.id, paint.id);
        assert.equal(worn.has(OTHER), false);
      });

      it("handles an empty request", () => {
        assert.equal(paints.wornByMany([]).size, 0);
      });

      it("agrees with the single lookup", () => {
        const paint = paints.create(AUTHOR, "Public", flat);
        paints.setShared(paint.id, AUTHOR, true);
        paints.wear(AUTHOR, paint.id);
        paints.wear(OTHER, paint.id);

        const worn = paints.wornByMany([AUTHOR, OTHER]);

        assert.equal(worn.get(AUTHOR)?.id, paints.wornBy(AUTHOR)?.id);
        assert.equal(worn.get(OTHER)?.id, paints.wornBy(OTHER)?.id);
      });
    });
  });
});
