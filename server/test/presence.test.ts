import assert from "node:assert/strict";
import { beforeEach, describe, it } from "node:test";
import { openDatabase, type Db } from "../src/db.ts";
import { Presence, TypingTracker } from "../src/modules/presence.ts";

const TIMEOUT_MS = 90_000;

function seedUser(db: Db, twitchId: string, login: string): void {
  db.prepare(
    `INSERT INTO users (twitch_id, login, display_name, first_seen_at, last_seen_at)
     VALUES (?, ?, ?, 0, 0)`,
  ).run(twitchId, login, login);
}

describe("Presence", () => {
  let db: Db;
  let clock: number;
  let presence: Presence;

  beforeEach(() => {
    db = openDatabase(":memory:");
    clock = 1_700_000_000_000;
    presence = new Presence(db, TIMEOUT_MS, () => clock);
    seedUser(db, "1", "alice");
    seedUser(db, "2", "bob");
  });

  it("reports unknown for an account never seen", () => {
    assert.equal(presence.stateOf("404"), "unknown");
  });

  it("reports online right after a heartbeat", () => {
    presence.beat("1", "5.0.0", false);
    assert.equal(presence.stateOf("1"), "online");
  });

  it("reports offline once the heartbeat goes stale", () => {
    presence.beat("1", "5.0.0", false);

    clock += TIMEOUT_MS + 1;

    assert.equal(presence.stateOf("1"), "offline");
  });

  it("stays online while heartbeats keep arriving", () => {
    presence.beat("1", "5.0.0", false);

    for (let index = 0; index < 5; index += 1) {
      clock += TIMEOUT_MS - 1000;
      presence.beat("1", "5.0.0", false);
      assert.equal(presence.stateOf("1"), "online");
    }
  });

  it("reports offline immediately after release", () => {
    presence.beat("1", "5.0.0", false);
    presence.release("1");

    assert.equal(presence.stateOf("1"), "offline");
  });

  it("hides a user who opted out, even while they are connected", () => {
    presence.beat("1", "5.0.0", true);
    assert.equal(presence.stateOf("1"), "unknown");
  });

  it("stops hiding once the user opts back in", () => {
    presence.beat("1", "5.0.0", true);
    presence.beat("1", "5.0.0", false);

    assert.equal(presence.stateOf("1"), "online");
  });

  describe("batch lookup", () => {
    it("returns an entry for every id asked about", () => {
      presence.beat("1", "5.0.0", false);

      const states = presence.statesOf(["1", "2", "404"]);

      assert.equal(states.get("1"), "online");
      assert.equal(states.get("2"), "unknown");
      assert.equal(states.get("404"), "unknown");
    });

    it("handles an empty request", () => {
      assert.equal(presence.statesOf([]).size, 0);
    });

    it("deduplicates repeated ids", () => {
      presence.beat("1", "5.0.0", false);

      const states = presence.statesOf(["1", "1", "1"]);

      assert.equal(states.size, 1);
      assert.equal(states.get("1"), "online");
    });

    it("agrees with the single lookup for mixed states", () => {
      presence.beat("1", "5.0.0", false);
      presence.beat("2", "5.0.0", false);
      presence.release("2");

      const states = presence.statesOf(["1", "2"]);

      assert.equal(states.get("1"), presence.stateOf("1"));
      assert.equal(states.get("2"), presence.stateOf("2"));
      assert.equal(states.get("2"), "offline");
    });
  });
});

describe("TypingTracker", () => {
  let clock: number;
  let typing: TypingTracker;

  beforeEach(() => {
    clock = 0;
    typing = new TypingTracker(8000, () => clock);
  });

  it("starts with nobody typing", () => {
    assert.deepEqual(typing.active("12345"), []);
  });

  it("records someone typing", () => {
    typing.set("12345", "alice", true);
    assert.deepEqual(typing.active("12345"), ["alice"]);
  });

  it("expires an entry after the ttl", () => {
    typing.set("12345", "alice", true);

    clock += 8001;

    assert.deepEqual(typing.active("12345"), []);
  });

  it("refreshes the ttl on a repeated signal", () => {
    typing.set("12345", "alice", true);

    clock += 7000;
    typing.set("12345", "alice", true);
    clock += 7000;

    assert.deepEqual(typing.active("12345"), ["alice"]);
  });

  it("clears an entry when the input box empties", () => {
    typing.set("12345", "alice", true);
    typing.set("12345", "alice", false);

    assert.deepEqual(typing.active("12345"), []);
  });

  it("keeps channels separate", () => {
    typing.set("12345", "alice", true);
    typing.set("67890", "bob", true);

    assert.deepEqual(typing.active("12345"), ["alice"]);
    assert.deepEqual(typing.active("67890"), ["bob"]);
  });

  it("tracks several people in one channel", () => {
    typing.set("12345", "alice", true);
    typing.set("12345", "bob", true);

    assert.deepEqual(typing.active("12345").sort(), ["alice", "bob"]);
  });

  it("tolerates clearing someone who was never typing", () => {
    typing.set("12345", "ghost", false);
    assert.deepEqual(typing.active("12345"), []);
  });
});
