import assert from "node:assert/strict";
import { beforeEach, describe, it } from "node:test";
import type {
  FastifyInstance,
  InjectOptions,
  LightMyRequestResponse,
} from "fastify";

/** The response shape `inject` produces; its chainable type does not narrow. */
type Injected = LightMyRequestResponse;
import { loadConfig } from "../src/config.ts";
import { openDatabase, type Db } from "../src/db.ts";
import { buildApi } from "../src/http.ts";
import { Hub } from "../src/hub.ts";
import { GlobalBans } from "../src/modules/globalBans.ts";
import { Paints } from "../src/modules/paints.ts";
import { Presence, TypingTracker } from "../src/modules/presence.ts";
import { ShadowChat } from "../src/modules/shadow.ts";
import { Users } from "../src/modules/users.ts";
import { FakeModeration, FakeValidator } from "./helpers.ts";

const CHANNEL = "1001";
const OTHER_CHANNEL = "1002";
const OFFENDER = "999";
/** Alice moderates CHANNEL; Bob moderates nothing. */
const ALICE = "1";
const BOB = "2";
const ALICE_AUTH = "Bearer alice-token";
const BOB_AUTH = "Bearer bob-token";

const flat = {
  kind: "flat" as const,
  stops: [{ at: 0, colour: "#00ff00ff" }],
};

describe("HTTP API", () => {
  let app: FastifyInstance;
  let db: Db;
  let bans: GlobalBans;
  let paints: Paints;
  let shadow: ShadowChat;
  let presence: Presence;
  let users: Users;

  beforeEach(async () => {
    db = openDatabase(":memory:");
    const config = loadConfig({ DATABASE_PATH: ":memory:" });

    const validator = new FakeValidator()
      .add("alice-token", ALICE, "alice")
      .add("bob-token", BOB, "bob");
    const moderation = new FakeModeration().grant(ALICE, CHANNEL);

    users = new Users(db);
    bans = new GlobalBans(db);
    paints = new Paints(db);
    shadow = new ShadowChat(db, config.maxShadowMessageLength);
    presence = new Presence(db, config.presenceTimeoutMs);

    const hub = new Hub({
      validator,
      users,
      shadow,
      presence,
      typing: new TypingTracker(),
    });

    app = await buildApi({
      config,
      validator,
      moderation,
      users,
      bans,
      paints,
      presence,
      shadow,
      hub,
    });
  });

  async function request(
    method: "GET" | "POST" | "PATCH" | "PUT" | "DELETE",
    url: string,
    auth: string | undefined,
    payload?: unknown,
  ): Promise<Injected> {
    const options: InjectOptions = { method, url };
    if (auth !== undefined) {
      options.headers = { authorization: auth };
    }
    if (payload !== undefined) {
      options.payload = payload as NonNullable<InjectOptions["payload"]>;
    }

    return await app.inject(options);
  }

  function get(url: string, auth?: string): Promise<Injected> {
    return request("GET", url, auth);
  }

  function send(
    method: "POST" | "PATCH" | "PUT" | "DELETE",
    url: string,
    auth: string | undefined,
    payload?: unknown,
  ): Promise<Injected> {
    return request(method, url, auth, payload);
  }

  it("serves health without authentication", async () => {
    const response = await get("/health");

    assert.equal(response.statusCode, 200);
    assert.equal(response.json().ok, true);
  });

  describe("authentication", () => {
    it("rejects a request with no token", async () => {
      assert.equal((await get(`/v1/bans/${OFFENDER}`)).statusCode, 401);
    });

    it("rejects a request with an unknown token", async () => {
      const response = await get(`/v1/bans/${OFFENDER}`, "Bearer forged");
      assert.equal(response.statusCode, 401);
    });

    it("accepts a valid token", async () => {
      assert.equal((await get(`/v1/bans/${OFFENDER}`, ALICE_AUTH)).statusCode, 200);
    });
  });

  describe("ban registry", () => {
    it("records a ban on a channel the caller moderates", async () => {
      const response = await send("POST", "/v1/bans", ALICE_AUTH, {
        offenderId: OFFENDER,
        channelId: CHANNEL,
        channelLogin: "alice",
        reason: "spam",
      });

      assert.equal(response.statusCode, 201);
      assert.equal(bans.historyFor(OFFENDER).length, 1);
    });

    it("refuses to record a ban on a channel the caller does not moderate", async () => {
      const response = await send("POST", "/v1/bans", BOB_AUTH, {
        offenderId: OFFENDER,
        channelId: CHANNEL,
        channelLogin: "alice",
      });

      assert.equal(response.statusCode, 403);
      assert.equal(bans.historyFor(OFFENDER).length, 0);
    });

    it("lets a broadcaster record a ban on their own channel", async () => {
      // Alice does not moderate OTHER_CHANNEL, but a broadcaster whose user id
      // equals the channel id is always in charge of it.
      const validator = new FakeValidator().add("own-token", CHANNEL, "chan");
      const own = await buildApi({
        config: loadConfig({ DATABASE_PATH: ":memory:" }),
        validator,
        moderation: new FakeModeration(),
        users,
        bans,
        paints,
        presence,
        shadow,
        hub: new Hub({
          validator,
          users,
          shadow,
          presence,
          typing: new TypingTracker(),
        }),
      });

      const response = await own.inject({
        method: "POST",
        url: "/v1/bans",
        headers: { authorization: "Bearer own-token" },
        payload: {
          offenderId: OFFENDER,
          channelId: CHANNEL,
          channelLogin: "chan",
        },
      });

      assert.equal(response.statusCode, 201);
    });

    it("rejects a malformed ban body", async () => {
      const response = await send("POST", "/v1/bans", ALICE_AUTH, {
        offenderId: "not-an-id",
        channelId: CHANNEL,
        channelLogin: "alice",
      });

      assert.equal(response.statusCode, 400);
    });

    it("reports the marker count excluding the viewing channel", async () => {
      bans.record({
        offenderId: OFFENDER,
        channelId: CHANNEL,
        channelLogin: "alice",
      });
      bans.record({
        offenderId: OFFENDER,
        channelId: OTHER_CHANNEL,
        channelLogin: "bob",
      });

      const seenElsewhere = await get(`/v1/bans/${OFFENDER}?channel=1003`, ALICE_AUTH);
      assert.equal(seenElsewhere.json().markerCount, 2);

      const seenOnChannel = await get(
        `/v1/bans/${OFFENDER}?channel=${CHANNEL}`,
        ALICE_AUTH,
      );
      assert.equal(seenOnChannel.json().markerCount, 1);
    });

    it("rejects an offender id that is not a Twitch id", async () => {
      assert.equal((await get("/v1/bans/bogus", ALICE_AUTH)).statusCode, 400);
    });

    it("returns batch markers for several offenders", async () => {
      bans.record({
        offenderId: OFFENDER,
        channelId: CHANNEL,
        channelLogin: "alice",
      });

      const response = await get(
        `/v1/bans?ids=${OFFENDER},888&channel=${OTHER_CHANNEL}`,
        ALICE_AUTH,
      );

      assert.equal(response.json().markers[OFFENDER], 1);
      assert.equal(response.json().markers["888"], undefined);
    });

    it("lets a moderator vouch for someone on their channel", async () => {
      bans.record({
        offenderId: OFFENDER,
        channelId: CHANNEL,
        channelLogin: "alice",
      });

      const response = await send("POST", `/v1/bans/${OFFENDER}/clear`, ALICE_AUTH, {
        channelId: CHANNEL,
      });

      assert.equal(response.statusCode, 200);
      assert.equal(response.json().changed, true);
      assert.equal(bans.markerCount(OFFENDER, OTHER_CHANNEL), 0);
    });

    it("refuses a vouch on a channel the caller does not moderate", async () => {
      bans.record({
        offenderId: OFFENDER,
        channelId: CHANNEL,
        channelLogin: "alice",
      });

      const response = await send("POST", `/v1/bans/${OFFENDER}/clear`, BOB_AUTH, {
        channelId: CHANNEL,
      });

      assert.equal(response.statusCode, 403);
      assert.equal(bans.markerCount(OFFENDER, OTHER_CHANNEL), 1);
    });
  });

  describe("shadow chat moderation", () => {
    it("lets a moderator restrict someone", async () => {
      const response = await send(
        "POST",
        `/v1/shadow/${CHANNEL}/restrict`,
        ALICE_AUTH,
        { userId: OFFENDER, durationMs: 60_000, reason: "spam" },
      );

      assert.equal(response.statusCode, 200);
      assert.notEqual(shadow.restrictionFor(CHANNEL, OFFENDER), null);
    });

    it("accepts a permanent ban as a null duration", async () => {
      const response = await send(
        "POST",
        `/v1/shadow/${CHANNEL}/restrict`,
        ALICE_AUTH,
        { userId: OFFENDER, durationMs: null },
      );

      assert.equal(response.statusCode, 200);
      assert.equal(shadow.restrictionFor(CHANNEL, OFFENDER)?.until, null);
    });

    it("refuses a restriction from a non-moderator", async () => {
      const response = await send(
        "POST",
        `/v1/shadow/${CHANNEL}/restrict`,
        BOB_AUTH,
        { userId: OFFENDER, durationMs: 60_000 },
      );

      assert.equal(response.statusCode, 403);
      assert.equal(shadow.restrictionFor(CHANNEL, OFFENDER), null);
    });

    it("rejects a negative duration", async () => {
      const response = await send(
        "POST",
        `/v1/shadow/${CHANNEL}/restrict`,
        ALICE_AUTH,
        { userId: OFFENDER, durationMs: -5 },
      );

      assert.equal(response.statusCode, 400);
    });

    it("lets a moderator lift a restriction", async () => {
      shadow.restrict(CHANNEL, OFFENDER, ALICE, null);

      const response = await send(
        "DELETE",
        `/v1/shadow/${CHANNEL}/restrict/${OFFENDER}`,
        ALICE_AUTH,
      );

      assert.equal(response.statusCode, 200);
      assert.equal(shadow.restrictionFor(CHANNEL, OFFENDER), null);
    });

    it("refuses a lift from a non-moderator", async () => {
      shadow.restrict(CHANNEL, OFFENDER, ALICE, null);

      const response = await send(
        "DELETE",
        `/v1/shadow/${CHANNEL}/restrict/${OFFENDER}`,
        BOB_AUTH,
      );

      assert.equal(response.statusCode, 403);
      assert.notEqual(shadow.restrictionFor(CHANNEL, OFFENDER), null);
    });
  });

  describe("presence", () => {
    it("reports states for the requested ids", async () => {
      users.remember({
        twitchId: ALICE,
        login: "alice",
        displayName: "alice",
        scopes: [],
      });
      presence.beat(ALICE, "5.0.0", false);

      const response = await get(`/v1/presence?ids=${ALICE},${BOB}`, ALICE_AUTH);

      assert.equal(response.json().presence[ALICE], "online");
      assert.equal(response.json().presence[BOB], "unknown");
    });

    it("handles a request with no ids", async () => {
      const response = await get("/v1/presence", ALICE_AUTH);

      assert.equal(response.statusCode, 200);
      assert.deepEqual(response.json().presence, {});
    });

    it("ignores ids that are not Twitch ids", async () => {
      const response = await get("/v1/presence?ids=abc,../etc", ALICE_AUTH);

      assert.deepEqual(response.json().presence, {});
    });
  });

  describe("paints", () => {
    it("creates a paint, private by default", async () => {
      const response = await send("POST", "/v1/paints", ALICE_AUTH, {
        name: "Lime",
        definition: flat,
      });

      assert.equal(response.statusCode, 201);
      assert.equal(response.json().shared, false);
      assert.equal(response.json().authorId, ALICE);
    });

    it("rejects an invalid definition", async () => {
      const response = await send("POST", "/v1/paints", ALICE_AUTH, {
        name: "Bad",
        definition: { kind: "flat", stops: [{ at: 0, colour: "red" }] },
      });

      assert.equal(response.statusCode, 400);
    });

    it("keeps an unshared paint out of the library", async () => {
      await send("POST", "/v1/paints", ALICE_AUTH, {
        name: "Private",
        definition: flat,
      });

      const response = await get("/v1/paints/library", ALICE_AUTH);
      assert.deepEqual(response.json().paints, []);
    });

    it("publishes a paint to the library", async () => {
      const created = await send("POST", "/v1/paints", ALICE_AUTH, {
        name: "Public",
        definition: flat,
      });
      const paintId = created.json().id;

      const shared = await send("PATCH", `/v1/paints/${paintId}`, ALICE_AUTH, {
        shared: true,
      });
      assert.equal(shared.statusCode, 200);

      const library = await get("/v1/paints/library", ALICE_AUTH);
      assert.equal(library.json().paints.length, 1);
    });

    it("refuses to publish someone else's paint", async () => {
      const created = await send("POST", "/v1/paints", ALICE_AUTH, {
        name: "Mine",
        definition: flat,
      });

      const response = await send(
        "PATCH",
        `/v1/paints/${created.json().id}`,
        BOB_AUTH,
        { shared: true },
      );

      assert.equal(response.statusCode, 403);
    });

    it("reports a missing paint as not found", async () => {
      const response = await send("PATCH", "/v1/paints/no-such", ALICE_AUTH, {
        name: "Nope",
      });

      assert.equal(response.statusCode, 404);
    });

    it("lists only the caller's own paints", async () => {
      await send("POST", "/v1/paints", ALICE_AUTH, {
        name: "Alice's",
        definition: flat,
      });

      const mine = await get("/v1/paints/mine", ALICE_AUTH);
      const theirs = await get("/v1/paints/mine", BOB_AUTH);

      assert.equal(mine.json().paints.length, 1);
      assert.equal(theirs.json().paints.length, 0);
    });

    it("wears and clears a paint", async () => {
      const created = await send("POST", "/v1/paints", ALICE_AUTH, {
        name: "Mine",
        definition: flat,
      });
      const paintId = created.json().id;

      const worn = await send("PUT", "/v1/paints/worn", ALICE_AUTH, { paintId });
      assert.equal(worn.json().paint.id, paintId);

      const cleared = await send("PUT", "/v1/paints/worn", ALICE_AUTH, {
        paintId: null,
      });
      assert.equal(cleared.json().paint, null);
    });

    it("refuses to wear another author's unshared paint", async () => {
      const created = await send("POST", "/v1/paints", ALICE_AUTH, {
        name: "Mine",
        definition: flat,
      });

      const response = await send("PUT", "/v1/paints/worn", BOB_AUTH, {
        paintId: created.json().id,
      });

      assert.equal(response.statusCode, 403);
    });

    it("deletes a paint the caller authored", async () => {
      const created = await send("POST", "/v1/paints", ALICE_AUTH, {
        name: "Doomed",
        definition: flat,
      });

      const response = await send(
        "DELETE",
        `/v1/paints/${created.json().id}`,
        ALICE_AUTH,
      );

      assert.equal(response.statusCode, 200);
      assert.equal(paints.get(created.json().id), null);
    });

    it("reports paints worn by several users at once", async () => {
      const created = await send("POST", "/v1/paints", ALICE_AUTH, {
        name: "Mine",
        definition: flat,
      });
      await send("PUT", "/v1/paints/worn", ALICE_AUTH, {
        paintId: created.json().id,
      });

      const response = await get(`/v1/paints/worn?ids=${ALICE},${BOB}`, ALICE_AUTH);

      assert.equal(response.json().worn[ALICE].id, created.json().id);
      assert.equal(response.json().worn[BOB], undefined);
    });
  });
});
