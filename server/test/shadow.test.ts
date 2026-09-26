import assert from "node:assert/strict";
import { beforeEach, describe, it } from "node:test";
import { openDatabase, type Db } from "../src/db.ts";
import { ShadowChat, ShadowRejection } from "../src/modules/shadow.ts";
import type { ShadowAuthor } from "../src/protocol.ts";

const CHANNEL = "12345";
const author: ShadowAuthor = { id: "999", login: "viewer", displayName: "Viewer" };
const moderator = "111";

describe("ShadowChat", () => {
  let db: Db;
  let clock: number;
  let chat: ShadowChat;

  beforeEach(() => {
    db = openDatabase(":memory:");
    clock = 1_700_000_000_000;
    chat = new ShadowChat(db, 100, () => clock);
  });

  it("stores a message and returns it in history", () => {
    const posted = chat.post(CHANNEL, author, "hello there");

    assert.equal(posted.body, "hello there");
    assert.equal(posted.channel, CHANNEL);
    assert.equal(posted.author.login, "viewer");

    const history = chat.history(CHANNEL);
    assert.equal(history.length, 1);
    assert.equal(history[0]?.id, posted.id);
  });

  it("returns history oldest first", () => {
    chat.post(CHANNEL, author, "first");
    clock += 1000;
    chat.post(CHANNEL, author, "second");
    clock += 1000;
    chat.post(CHANNEL, author, "third");

    const bodies = chat.history(CHANNEL).map((message) => message.body);
    assert.deepEqual(bodies, ["first", "second", "third"]);
  });

  it("keeps rooms separate", () => {
    chat.post(CHANNEL, author, "in one room");
    chat.post("67890", author, "in another");

    assert.equal(chat.history(CHANNEL).length, 1);
    assert.equal(chat.history("67890").length, 1);
    assert.equal(chat.history(CHANNEL)[0]?.body, "in one room");
  });

  it("trims surrounding whitespace", () => {
    const posted = chat.post(CHANNEL, author, "   padded   ");
    assert.equal(posted.body, "padded");
  });

  it("rejects a message that is only whitespace", () => {
    assert.throws(
      () => chat.post(CHANNEL, author, "    "),
      (error: unknown) =>
        error instanceof ShadowRejection && error.code === "bad_frame",
    );
  });

  it("rejects a message over the length limit", () => {
    assert.throws(
      () => chat.post(CHANNEL, author, "x".repeat(101)),
      (error: unknown) => error instanceof ShadowRejection && error.code === "too_long",
    );
  });

  it("accepts a message exactly at the length limit", () => {
    const posted = chat.post(CHANNEL, author, "x".repeat(100));
    assert.equal(posted.body.length, 100);
  });

  describe("restrictions", () => {
    it("reports no restriction for an unrestricted user", () => {
      assert.equal(chat.restrictionFor(CHANNEL, author.id), null);
    });

    it("blocks a timed-out user from posting", () => {
      chat.restrict(CHANNEL, author.id, moderator, 60_000, "spam");

      assert.throws(
        () => chat.post(CHANNEL, author, "let me through"),
        (error: unknown) =>
          error instanceof ShadowRejection && error.code === "restricted",
      );
    });

    it("lets the user speak again once the timeout expires", () => {
      chat.restrict(CHANNEL, author.id, moderator, 60_000);

      clock += 60_001;

      const posted = chat.post(CHANNEL, author, "back again");
      assert.equal(posted.body, "back again");
      assert.equal(chat.restrictionFor(CHANNEL, author.id), null);
    });

    it("keeps a permanent ban in force indefinitely", () => {
      chat.restrict(CHANNEL, author.id, moderator, null, "bye");

      clock += 365 * 24 * 60 * 60 * 1000;

      const restriction = chat.restrictionFor(CHANNEL, author.id);
      assert.equal(restriction?.until, null);
      assert.equal(restriction?.reason, "bye");
    });

    it("scopes a restriction to one channel", () => {
      chat.restrict(CHANNEL, author.id, moderator, null);

      const posted = chat.post("67890", author, "other room is fine");
      assert.equal(posted.body, "other room is fine");
    });

    it("replaces an existing restriction rather than duplicating it", () => {
      chat.restrict(CHANNEL, author.id, moderator, 60_000, "first");
      chat.restrict(CHANNEL, author.id, moderator, null, "second");

      const restriction = chat.restrictionFor(CHANNEL, author.id);
      assert.equal(restriction?.until, null);
      assert.equal(restriction?.reason, "second");
    });

    it("lifts a restriction on unban", () => {
      chat.restrict(CHANNEL, author.id, moderator, null);
      chat.lift(CHANNEL, author.id);

      assert.equal(chat.restrictionFor(CHANNEL, author.id), null);
      assert.equal(chat.post(CHANNEL, author, "thanks").body, "thanks");
    });
  });

  it("prunes messages past the retention window", () => {
    chat.post(CHANNEL, author, "ancient");

    clock += 25 * 60 * 60 * 1000;
    chat.post(CHANNEL, author, "recent");

    const bodies = chat.history(CHANNEL).map((message) => message.body);
    assert.deepEqual(bodies, ["recent"]);
  });

  it("caps history at the requested limit", () => {
    for (let index = 0; index < 10; index += 1) {
      chat.post(CHANNEL, author, `message ${index}`);
      clock += 10;
    }

    const history = chat.history(CHANNEL, 3);
    assert.equal(history.length, 3);
    assert.deepEqual(
      history.map((message) => message.body),
      ["message 7", "message 8", "message 9"],
    );
  });
});
