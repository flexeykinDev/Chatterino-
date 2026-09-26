import assert from "node:assert/strict";
import { beforeEach, describe, it } from "node:test";
import { AuthError, type Identity, type Validator } from "../src/auth.ts";
import { openDatabase, type Db } from "../src/db.ts";
import { Hub, type Socket } from "../src/hub.ts";
import { GlobalBans } from "../src/modules/globalBans.ts";
import { Presence, TypingTracker } from "../src/modules/presence.ts";
import { ShadowChat } from "../src/modules/shadow.ts";
import { Users } from "../src/modules/users.ts";
import type { ServerFrame } from "../src/protocol.ts";

const CHANNEL = "12345";

/** A socket that records what was written to it. */
class FakeSocket implements Socket {
  readonly sent: ServerFrame[] = [];
  closed = false;
  /** When set, send() throws, simulating a dead peer. */
  failOnSend = false;

  send(payload: string): void {
    if (this.failOnSend) {
      throw new Error("peer is gone");
    }
    this.sent.push(JSON.parse(payload) as ServerFrame);
  }

  close(): void {
    this.closed = true;
  }

  /** The frames of one type, in order. */
  ofType<T extends ServerFrame["t"]>(
    type: T,
  ): Extract<ServerFrame, { t: T }>[] {
    return this.sent.filter(
      (frame): frame is Extract<ServerFrame, { t: T }> => frame.t === type,
    );
  }

  last(): ServerFrame | undefined {
    return this.sent.at(-1);
  }
}

class FakeValidator implements Validator {
  /** token -> identity. Anything else is rejected. */
  private readonly known = new Map<string, Identity>();

  add(token: string, twitchId: string, login: string): void {
    this.known.set(token, {
      twitchId,
      login,
      displayName: login,
      scopes: [],
    });
  }

  validate(token: string): Promise<Identity> {
    const identity = this.known.get(token);
    if (identity === undefined) {
      return Promise.reject(new AuthError("token rejected by Twitch", 401));
    }
    return Promise.resolve(identity);
  }
}

describe("Hub", () => {
  let db: Db;
  let clock: number;
  let hub: Hub;
  let validator: FakeValidator;
  let shadow: ShadowChat;
  let presence: Presence;
  let typing: TypingTracker;
  let users: Users;
  let errors: unknown[];

  beforeEach(() => {
    db = openDatabase(":memory:");
    clock = 1_700_000_000_000;
    validator = new FakeValidator();
    validator.add("alice-token", "1", "alice");
    validator.add("bob-token", "2", "bob");

    shadow = new ShadowChat(db, 100, () => clock);
    presence = new Presence(db, 90_000, () => clock);
    typing = new TypingTracker(8000, () => clock);
    users = new Users(db, () => clock);
    errors = [];

    hub = new Hub({
      validator,
      users,
      shadow,
      presence,
      typing,
      now: () => clock,
      onError: (error) => errors.push(error),
    });
  });

  /** Connects a socket and completes the handshake. */
  async function connect(token: string): Promise<FakeSocket> {
    const socket = new FakeSocket();
    hub.add(socket);
    await hub.handle(socket, JSON.stringify({ t: "hello", token }));
    return socket;
  }

  async function send(socket: FakeSocket, frame: unknown): Promise<void> {
    await hub.handle(socket, JSON.stringify(frame));
  }

  describe("handshake", () => {
    it("acknowledges a valid token", async () => {
      const socket = await connect("alice-token");

      const ready = socket.ofType("ready");
      assert.equal(ready.length, 1);
      assert.equal(ready[0]?.user.login, "alice");
      assert.equal(socket.closed, false);
    });

    it("marks the account online", async () => {
      await connect("alice-token");
      assert.equal(presence.stateOf("1"), "online");
    });

    it("rejects and closes on a bad token", async () => {
      const socket = new FakeSocket();
      hub.add(socket);
      await send(socket, { t: "hello", token: "forged" });

      const failure = socket.ofType("error")[0];
      assert.equal(failure?.code, "auth_failed");
      assert.equal(socket.closed, true);
    });

    it("refuses a second hello on the same socket", async () => {
      const socket = await connect("alice-token");
      await send(socket, { t: "hello", token: "bob-token" });

      assert.equal(socket.ofType("error")[0]?.code, "already_authenticated");
    });

    it("refuses other frames before hello", async () => {
      const socket = new FakeSocket();
      hub.add(socket);
      await send(socket, { t: "join", channel: CHANNEL });

      assert.equal(socket.ofType("error")[0]?.code, "unauthenticated");
    });

    it("reports a malformed frame without closing", async () => {
      const socket = await connect("alice-token");
      await hub.handle(socket, "{not json");

      assert.equal(socket.ofType("error")[0]?.code, "bad_frame");
      assert.equal(socket.closed, false);
    });

    it("honours a request to hide presence", async () => {
      const socket = new FakeSocket();
      hub.add(socket);
      await send(socket, { t: "hello", token: "alice-token", hidePresence: true });

      assert.equal(presence.stateOf("1"), "unknown");
    });
  });

  describe("rooms", () => {
    it("sends history on join", async () => {
      shadow.post(CHANNEL, { id: "9", login: "earlier", displayName: "earlier" }, "hi");

      const socket = await connect("alice-token");
      await send(socket, { t: "join", channel: CHANNEL });

      const joined = socket.ofType("joined")[0];
      assert.equal(joined?.channel, CHANNEL);
      assert.equal(joined?.history.length, 1);
      assert.equal(joined?.history[0]?.body, "hi");
    });

    it("counts one member per joined socket", async () => {
      const alice = await connect("alice-token");
      const bob = await connect("bob-token");

      await send(alice, { t: "join", channel: CHANNEL });
      await send(bob, { t: "join", channel: CHANNEL });

      assert.equal(hub.roomSize(CHANNEL), 2);
    });

    it("does not double-count a repeated join", async () => {
      const socket = await connect("alice-token");
      await send(socket, { t: "join", channel: CHANNEL });
      await send(socket, { t: "join", channel: CHANNEL });

      assert.equal(hub.roomSize(CHANNEL), 1);
    });

    it("leaves the room on part", async () => {
      const socket = await connect("alice-token");
      await send(socket, { t: "join", channel: CHANNEL });
      await send(socket, { t: "part", channel: CHANNEL });

      assert.equal(hub.roomSize(CHANNEL), 0);
      assert.equal(socket.ofType("parted").length, 1);
    });

    it("leaves every room when the socket drops", async () => {
      const socket = await connect("alice-token");
      await send(socket, { t: "join", channel: CHANNEL });
      await send(socket, { t: "join", channel: "67890" });

      hub.remove(socket);

      assert.equal(hub.roomSize(CHANNEL), 0);
      assert.equal(hub.roomSize("67890"), 0);
      assert.equal(hub.connectionCount, 0);
    });

    it("tells a joiner they are already restricted", async () => {
      shadow.restrict(CHANNEL, "1", "moderator", 60_000, "cool off");

      const socket = await connect("alice-token");
      await send(socket, { t: "join", channel: CHANNEL });

      const restricted = socket.ofType("restricted")[0];
      assert.equal(restricted?.channel, CHANNEL);
      assert.equal(restricted?.reason, "cool off");
    });
  });

  describe("messages", () => {
    it("delivers a message to everyone in the room", async () => {
      const alice = await connect("alice-token");
      const bob = await connect("bob-token");
      await send(alice, { t: "join", channel: CHANNEL });
      await send(bob, { t: "join", channel: CHANNEL });

      await send(alice, { t: "say", channel: CHANNEL, body: "hello room" });

      assert.equal(bob.ofType("message")[0]?.body, "hello room");
      // The sender sees it too, so their own message appears in order.
      assert.equal(alice.ofType("message")[0]?.body, "hello room");
    });

    it("does not deliver to a different room", async () => {
      const alice = await connect("alice-token");
      const bob = await connect("bob-token");
      await send(alice, { t: "join", channel: CHANNEL });
      await send(bob, { t: "join", channel: "67890" });

      await send(alice, { t: "say", channel: CHANNEL, body: "hello" });

      assert.equal(bob.ofType("message").length, 0);
    });

    it("acknowledges a message that carried a nonce", async () => {
      const socket = await connect("alice-token");
      await send(socket, { t: "join", channel: CHANNEL });
      await send(socket, { t: "say", channel: CHANNEL, body: "hi", nonce: "n1" });

      const ack = socket.ofType("ack")[0];
      assert.equal(ack?.nonce, "n1");
      assert.ok(typeof ack?.id === "number");
    });

    it("refuses a message to a room it has not joined", async () => {
      const socket = await connect("alice-token");
      await send(socket, { t: "say", channel: CHANNEL, body: "hi" });

      assert.equal(socket.ofType("error")[0]?.code, "not_joined");
    });

    it("refuses a message from a restricted user and restates the restriction", async () => {
      const socket = await connect("alice-token");
      await send(socket, { t: "join", channel: CHANNEL });
      shadow.restrict(CHANNEL, "1", "moderator", 60_000, "spam");

      await send(socket, { t: "say", channel: CHANNEL, body: "let me in" });

      assert.equal(socket.ofType("error")[0]?.code, "restricted");
      assert.equal(socket.ofType("restricted")[0]?.reason, "spam");
    });

    it("refuses an overlong message", async () => {
      const socket = await connect("alice-token");
      await send(socket, { t: "join", channel: CHANNEL });
      await send(socket, { t: "say", channel: CHANNEL, body: "x".repeat(101) });

      assert.equal(socket.ofType("error")[0]?.code, "too_long");
    });

    it("rate limits a flood and recovers after the window", async () => {
      const socket = await connect("alice-token");
      await send(socket, { t: "join", channel: CHANNEL });

      for (let index = 0; index < 10; index += 1) {
        await send(socket, { t: "say", channel: CHANNEL, body: `m${index}` });
      }
      assert.equal(socket.ofType("error").length, 0);

      await send(socket, { t: "say", channel: CHANNEL, body: "one too many" });
      assert.equal(socket.ofType("error")[0]?.code, "rate_limited");

      clock += 10_001;
      await send(socket, { t: "say", channel: CHANNEL, body: "later" });
      assert.equal(socket.ofType("error").length, 1);
    });
  });

  describe("typing indicators", () => {
    it("tells others in the room, but not the typist", async () => {
      const alice = await connect("alice-token");
      const bob = await connect("bob-token");
      await send(alice, { t: "join", channel: CHANNEL });
      await send(bob, { t: "join", channel: CHANNEL });

      await send(alice, { t: "typing", channel: CHANNEL });

      assert.equal(bob.ofType("typing")[0]?.login, "alice");
      assert.equal(bob.ofType("typing")[0]?.active, true);
      assert.equal(alice.ofType("typing").length, 0);
    });

    it("clears the indicator once the message is sent", async () => {
      const alice = await connect("alice-token");
      const bob = await connect("bob-token");
      await send(alice, { t: "join", channel: CHANNEL });
      await send(bob, { t: "join", channel: CHANNEL });

      await send(alice, { t: "typing", channel: CHANNEL });
      await send(alice, { t: "say", channel: CHANNEL, body: "done" });

      assert.equal(bob.ofType("typing").at(-1)?.active, false);
      assert.deepEqual(typing.active(CHANNEL), []);
    });

    it("ignores a typing signal for a room it has not joined", async () => {
      const alice = await connect("alice-token");
      const bob = await connect("bob-token");
      await send(bob, { t: "join", channel: CHANNEL });

      await send(alice, { t: "typing", channel: CHANNEL });

      assert.equal(bob.ofType("typing").length, 0);
    });

    it("clears the indicator when the typist disconnects", async () => {
      const alice = await connect("alice-token");
      await send(alice, { t: "join", channel: CHANNEL });
      await send(alice, { t: "typing", channel: CHANNEL });

      hub.remove(alice);

      assert.deepEqual(typing.active(CHANNEL), []);
    });
  });

  describe("presence lifecycle", () => {
    it("keeps the account online on a heartbeat", async () => {
      const socket = await connect("alice-token");

      clock += 89_000;
      await send(socket, { t: "beat" });
      clock += 89_000;

      assert.equal(presence.stateOf("1"), "online");
    });

    it("goes offline when the last connection closes", async () => {
      const socket = await connect("alice-token");
      hub.remove(socket);

      assert.equal(presence.stateOf("1"), "offline");
    });

    it("stays online while another window is still open", async () => {
      const first = await connect("alice-token");
      await connect("alice-token");

      hub.remove(first);

      assert.equal(presence.stateOf("1"), "online");
    });
  });

  describe("pushed restrictions", () => {
    it("reaches the affected user's sockets in that room", async () => {
      const alice = await connect("alice-token");
      const bob = await connect("bob-token");
      await send(alice, { t: "join", channel: CHANNEL });
      await send(bob, { t: "join", channel: CHANNEL });

      hub.notifyRestricted(CHANNEL, "1", clock + 60_000, "timed out");

      assert.equal(alice.ofType("restricted")[0]?.reason, "timed out");
      assert.equal(bob.ofType("restricted").length, 0);
    });

    it("does not reach a room the user has not joined", async () => {
      const alice = await connect("alice-token");

      hub.notifyRestricted(CHANNEL, "1", null, "banned");

      assert.equal(alice.ofType("restricted").length, 0);
    });
  });

  describe("failing sockets", () => {
    it("drops a connection whose write fails", async () => {
      const alice = await connect("alice-token");
      const bob = await connect("bob-token");
      await send(alice, { t: "join", channel: CHANNEL });
      await send(bob, { t: "join", channel: CHANNEL });

      bob.failOnSend = true;
      await send(alice, { t: "say", channel: CHANNEL, body: "hello" });

      assert.equal(hub.roomSize(CHANNEL), 1);
      assert.equal(errors.length > 0, true);
    });

    it("still delivers to healthy peers when one fails", async () => {
      const alice = await connect("alice-token");
      const bob = await connect("bob-token");
      const carol = new FakeSocket();
      hub.add(carol);
      validator.add("carol-token", "3", "carol");
      await send(carol, { t: "hello", token: "carol-token" });

      for (const socket of [alice, bob, carol]) {
        await send(socket, { t: "join", channel: CHANNEL });
      }

      bob.failOnSend = true;
      await send(alice, { t: "say", channel: CHANNEL, body: "hello" });

      assert.equal(carol.ofType("message")[0]?.body, "hello");
    });

    it("ignores frames from a socket that was already removed", async () => {
      const socket = await connect("alice-token");
      hub.remove(socket);

      await send(socket, { t: "join", channel: CHANNEL });

      assert.equal(hub.roomSize(CHANNEL), 0);
    });
  });

  it("keeps the ban registry independent of the socket layer", () => {
    // The hub does not own ban policy; this guards the separation by checking
    // the registry works with no connections at all.
    const bans = new GlobalBans(db, () => clock);
    bans.record({ offenderId: "9", channelId: CHANNEL, channelLogin: "alice" });

    assert.equal(bans.markerCount("9", "67890"), 1);
    assert.equal(hub.connectionCount, 0);
  });
});
