import assert from "node:assert/strict";
import { describe, it } from "node:test";
import {
  ModerationError,
  parseModeratedPage,
  type ModerationLookup,
} from "../src/modules/moderation.ts";

describe("parseModeratedPage", () => {
  it("extracts broadcaster ids", () => {
    const page = parseModeratedPage({
      data: [
        { broadcaster_id: "1", broadcaster_login: "alice" },
        { broadcaster_id: "2", broadcaster_login: "bob" },
      ],
      pagination: {},
    });

    assert.deepEqual(page.channelIds, ["1", "2"]);
  });

  it("returns no cursor on the last page", () => {
    const page = parseModeratedPage({ data: [], pagination: {} });
    assert.equal(page.cursor, undefined);
  });

  it("treats an empty cursor string as no cursor", () => {
    const page = parseModeratedPage({ data: [], pagination: { cursor: "" } });
    assert.equal(page.cursor, undefined);
  });

  it("returns a cursor when more pages follow", () => {
    const page = parseModeratedPage({ data: [], pagination: { cursor: "abc" } });
    assert.equal(page.cursor, "abc");
  });

  it("tolerates a missing pagination object", () => {
    const page = parseModeratedPage({ data: [{ broadcaster_id: "1" }] });
    assert.equal(page.cursor, undefined);
    assert.deepEqual(page.channelIds, ["1"]);
  });

  it("skips entries with no usable broadcaster id", () => {
    const page = parseModeratedPage({
      data: [{ broadcaster_id: "1" }, { broadcaster_id: 2 }, {}, null, "nope"],
      pagination: {},
    });

    assert.deepEqual(page.channelIds, ["1"]);
  });

  it("rejects a response with no data array", () => {
    assert.throws(() => parseModeratedPage({ pagination: {} }), ModerationError);
  });

  it("rejects a non-object body", () => {
    assert.throws(() => parseModeratedPage("nope"), ModerationError);
    assert.throws(() => parseModeratedPage(null), ModerationError);
  });
});

/**
 * A stand-in for the Twitch-backed lookup, so route tests can grant or withhold
 * moderator status without a network call.
 */
export class FakeModeration implements ModerationLookup {
  /** user id -> channel ids they moderate. */
  private readonly grants = new Map<string, Set<string>>();

  grant(userId: string, channelId: string): void {
    let channels = this.grants.get(userId);
    if (channels === undefined) {
      channels = new Set();
      this.grants.set(userId, channels);
    }
    channels.add(channelId);
  }

  moderates(_token: string, userId: string, channelId: string): Promise<boolean> {
    if (userId === channelId) {
      return Promise.resolve(true);
    }
    return Promise.resolve(this.grants.get(userId)?.has(channelId) ?? false);
  }

  moderatedChannels(_token: string, userId: string): Promise<Set<string>> {
    return Promise.resolve(new Set(this.grants.get(userId) ?? []));
  }
}

describe("FakeModeration", () => {
  it("treats a broadcaster as moderating their own channel", async () => {
    const moderation = new FakeModeration();
    assert.equal(await moderation.moderates("t", "1", "1"), true);
  });

  it("withholds status that was not granted", async () => {
    const moderation = new FakeModeration();
    assert.equal(await moderation.moderates("t", "1", "2"), false);
  });

  it("honours a granted channel", async () => {
    const moderation = new FakeModeration();
    moderation.grant("1", "2");

    assert.equal(await moderation.moderates("t", "1", "2"), true);
    assert.deepEqual([...(await moderation.moderatedChannels("t", "1"))], ["2"]);
  });
});
