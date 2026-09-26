import assert from "node:assert/strict";
import { beforeEach, describe, it } from "node:test";
import { openDatabase, type Db } from "../src/db.ts";
import { GlobalBans } from "../src/modules/globalBans.ts";

const OFFENDER = "999";
const CHANNEL_A = "1001";
const CHANNEL_B = "1002";
const CHANNEL_C = "1003";
const MODERATOR = "555";

describe("GlobalBans", () => {
  let db: Db;
  let clock: number;
  let bans: GlobalBans;

  beforeEach(() => {
    db = openDatabase(":memory:");
    clock = 1_700_000_000_000;
    bans = new GlobalBans(db, () => clock);
  });

  function banOn(channelId: string, channelLogin = `channel${channelId}`) {
    return bans.record({
      offenderId: OFFENDER,
      channelId,
      channelLogin,
      reason: "spam",
      context: [{ body: "buy followers", sentAt: clock - 1000 }],
    });
  }

  it("records a ban with its context", () => {
    const record = banOn(CHANNEL_A, "alice");

    assert.equal(record.offenderId, OFFENDER);
    assert.equal(record.channelLogin, "alice");
    assert.equal(record.reason, "spam");
    assert.equal(record.liftedAt, null);
    assert.equal(record.clearedAt, null);
    assert.deepEqual(
      record.context.map((line) => line.body),
      ["buy followers"],
    );
  });

  it("returns history newest first with context attached", () => {
    banOn(CHANNEL_A);
    clock += 1000;
    banOn(CHANNEL_B);

    const history = bans.historyFor(OFFENDER);

    assert.equal(history.length, 2);
    assert.equal(history[0]?.channelId, CHANNEL_B);
    assert.equal(history[1]?.channelId, CHANNEL_A);
    assert.equal(history[0]?.context.length, 1);
  });

  it("returns an empty history for someone with no bans", () => {
    assert.deepEqual(bans.historyFor("no-such-user"), []);
  });

  it("keeps one row per channel when re-banning", () => {
    banOn(CHANNEL_A);
    clock += 5000;
    banOn(CHANNEL_A);

    assert.equal(bans.historyFor(OFFENDER).length, 1);
  });

  it("replaces context rather than accumulating it on re-ban", () => {
    bans.record({
      offenderId: OFFENDER,
      channelId: CHANNEL_A,
      channelLogin: "alice",
      context: [{ body: "old line", sentAt: clock }],
    });
    clock += 1000;
    bans.record({
      offenderId: OFFENDER,
      channelId: CHANNEL_A,
      channelLogin: "alice",
      context: [{ body: "new line", sentAt: clock }],
    });

    const history = bans.historyFor(OFFENDER);
    assert.deepEqual(
      history[0]?.context.map((line) => line.body),
      ["new line"],
    );
  });

  it("caps stored context lines", () => {
    const manyLines = Array.from({ length: 50 }, (_, index) => ({
      body: `line ${index}`,
      sentAt: clock + index,
    }));

    bans.record({
      offenderId: OFFENDER,
      channelId: CHANNEL_A,
      channelLogin: "alice",
      context: manyLines,
    });

    const stored = bans.historyFor(OFFENDER)[0]?.context ?? [];
    assert.equal(stored.length, 20);
    // The most recent lines are the ones worth keeping.
    assert.equal(stored.at(-1)?.body, "line 49");
  });

  it("orders context oldest first", () => {
    bans.record({
      offenderId: OFFENDER,
      channelId: CHANNEL_A,
      channelLogin: "alice",
      context: [
        { body: "second", sentAt: 2000 },
        { body: "first", sentAt: 1000 },
      ],
    });

    assert.deepEqual(
      bans.historyFor(OFFENDER)[0]?.context.map((line) => line.body),
      ["first", "second"],
    );
  });

  describe("marker count", () => {
    it("is zero for someone with no bans", () => {
      assert.equal(bans.markerCount(OFFENDER, CHANNEL_A), 0);
    });

    it("counts bans from other channels", () => {
      banOn(CHANNEL_A);
      banOn(CHANNEL_B);

      assert.equal(bans.markerCount(OFFENDER, CHANNEL_C), 2);
    });

    it("excludes a ban on the channel being viewed", () => {
      banOn(CHANNEL_A);
      banOn(CHANNEL_B);

      assert.equal(bans.markerCount(OFFENDER, CHANNEL_A), 1);
    });

    it("excludes a lifted ban", () => {
      banOn(CHANNEL_A);
      banOn(CHANNEL_B);
      bans.markLifted(OFFENDER, CHANNEL_A);

      assert.equal(bans.markerCount(OFFENDER, CHANNEL_C), 1);
    });

    it("excludes a ban that was vouched for", () => {
      banOn(CHANNEL_A);
      banOn(CHANNEL_B);
      bans.clear(OFFENDER, CHANNEL_A, MODERATOR);

      assert.equal(bans.markerCount(OFFENDER, CHANNEL_C), 1);
    });

    it("counts again once a re-ban overrides a vouch", () => {
      banOn(CHANNEL_A);
      bans.clear(OFFENDER, CHANNEL_A, MODERATOR);
      assert.equal(bans.markerCount(OFFENDER, CHANNEL_C), 0);

      clock += 1000;
      banOn(CHANNEL_A);

      assert.equal(bans.markerCount(OFFENDER, CHANNEL_C), 1);
    });

    it("counts again once a re-ban overrides a lift", () => {
      banOn(CHANNEL_A);
      bans.markLifted(OFFENDER, CHANNEL_A);
      assert.equal(bans.markerCount(OFFENDER, CHANNEL_C), 0);

      clock += 1000;
      banOn(CHANNEL_A);

      assert.equal(bans.markerCount(OFFENDER, CHANNEL_C), 1);
    });
  });

  describe("batch marker counts", () => {
    it("omits offenders with no marker", () => {
      banOn(CHANNEL_A);

      const counts = bans.markerCounts([OFFENDER, "clean-user"], CHANNEL_C);

      assert.equal(counts.get(OFFENDER), 1);
      assert.equal(counts.has("clean-user"), false);
    });

    it("handles an empty request", () => {
      assert.equal(bans.markerCounts([], CHANNEL_A).size, 0);
    });

    it("agrees with the single lookup", () => {
      banOn(CHANNEL_A);
      banOn(CHANNEL_B);
      bans.record({ offenderId: "888", channelId: CHANNEL_A, channelLogin: "alice" });

      const counts = bans.markerCounts([OFFENDER, "888"], CHANNEL_B);

      assert.equal(counts.get(OFFENDER), bans.markerCount(OFFENDER, CHANNEL_B));
      assert.equal(counts.get("888"), bans.markerCount("888", CHANNEL_B));
    });

    it("deduplicates repeated ids", () => {
      banOn(CHANNEL_A);

      const counts = bans.markerCounts([OFFENDER, OFFENDER], CHANNEL_C);

      assert.equal(counts.size, 1);
      assert.equal(counts.get(OFFENDER), 1);
    });
  });

  describe("lifting and vouching", () => {
    it("reports whether a lift changed anything", () => {
      banOn(CHANNEL_A);

      assert.equal(bans.markLifted(OFFENDER, CHANNEL_A), true);
      // Already lifted, so the second call is a no-op.
      assert.equal(bans.markLifted(OFFENDER, CHANNEL_A), false);
    });

    it("reports false when lifting a ban that does not exist", () => {
      assert.equal(bans.markLifted(OFFENDER, CHANNEL_A), false);
    });

    it("keeps a lifted ban in the history", () => {
      banOn(CHANNEL_A);
      bans.markLifted(OFFENDER, CHANNEL_A);

      const history = bans.historyFor(OFFENDER);
      assert.equal(history.length, 1);
      assert.equal(history[0]?.liftedAt, clock);
    });

    it("can restore a ban wrongly marked lifted", () => {
      banOn(CHANNEL_A);
      bans.markLifted(OFFENDER, CHANNEL_A);
      bans.markActive(OFFENDER, CHANNEL_A);

      assert.equal(bans.historyFor(OFFENDER)[0]?.liftedAt, null);
      assert.equal(bans.markerCount(OFFENDER, CHANNEL_C), 1);
    });

    it("attributes a vouch to the moderator who made it", () => {
      banOn(CHANNEL_A);
      bans.clear(OFFENDER, CHANNEL_A, MODERATOR);

      const record = bans.historyFor(OFFENDER)[0];
      assert.equal(record?.clearedBy, MODERATOR);
      assert.equal(record?.clearedAt, clock);
    });

    it("does not vouch twice", () => {
      banOn(CHANNEL_A);

      assert.equal(bans.clear(OFFENDER, CHANNEL_A, MODERATOR), true);
      assert.equal(bans.clear(OFFENDER, CHANNEL_A, MODERATOR), false);
    });

    it("can withdraw a vouch", () => {
      banOn(CHANNEL_A);
      bans.clear(OFFENDER, CHANNEL_A, MODERATOR);
      bans.unclear(OFFENDER, CHANNEL_A);

      assert.equal(bans.historyFor(OFFENDER)[0]?.clearedBy, null);
      assert.equal(bans.markerCount(OFFENDER, CHANNEL_C), 1);
    });

    it("keeps one channel's vouch from affecting another channel's view", () => {
      banOn(CHANNEL_A);
      bans.clear(OFFENDER, CHANNEL_A, MODERATOR);

      // A vouched-for ban stops counting everywhere, but the ban on B is
      // untouched by A's decision.
      banOn(CHANNEL_B);
      assert.equal(bans.markerCount(OFFENDER, CHANNEL_C), 1);
      assert.equal(bans.historyFor(OFFENDER).length, 2);
    });
  });

  describe("relay support", () => {
    it("lists channels where the ban still holds, newest first", () => {
      banOn(CHANNEL_A);
      clock += 1000;
      banOn(CHANNEL_B);

      assert.deepEqual(bans.activeChannelsFor(OFFENDER), [CHANNEL_B, CHANNEL_A]);
    });

    it("omits lifted bans from the relay list", () => {
      banOn(CHANNEL_A);
      banOn(CHANNEL_B);
      bans.markLifted(OFFENDER, CHANNEL_A);

      assert.deepEqual(bans.activeChannelsFor(OFFENDER), [CHANNEL_B]);
    });

    it("keeps a vouched-for ban in the relay list", () => {
      // A vouch hides the marker; it does not mean the ban was undone.
      banOn(CHANNEL_A);
      bans.clear(OFFENDER, CHANNEL_A, MODERATOR);

      assert.deepEqual(bans.activeChannelsFor(OFFENDER), [CHANNEL_A]);
    });
  });

  describe("revalidation queue", () => {
    it("returns bans older than the cutoff, oldest first", () => {
      banOn(CHANNEL_A);
      clock += 10_000;
      banOn(CHANNEL_B);

      // The cutoff is exclusive, so it must sit past the newest ban for both
      // to be considered stale.
      const stale = bans.staleBans(clock + 1);

      assert.equal(stale.length, 2);
      assert.equal(stale[0]?.channelId, CHANNEL_A);
    });

    it("treats the cutoff as exclusive", () => {
      banOn(CHANNEL_A);

      assert.deepEqual(bans.staleBans(clock), []);
    });

    it("excludes bans newer than the cutoff", () => {
      banOn(CHANNEL_A);

      assert.deepEqual(bans.staleBans(clock - 1), []);
    });

    it("excludes already lifted bans", () => {
      banOn(CHANNEL_A);
      bans.markLifted(OFFENDER, CHANNEL_A);

      assert.deepEqual(bans.staleBans(clock + 1), []);
    });

    it("honours the limit", () => {
      banOn(CHANNEL_A);
      banOn(CHANNEL_B);
      banOn(CHANNEL_C);

      assert.equal(bans.staleBans(clock + 1, 2).length, 2);
    });
  });
});
