import assert from "node:assert/strict";
import { describe, it } from "node:test";
import { parseClientFrame } from "../src/protocol.ts";

function parse(value: unknown) {
  return parseClientFrame(JSON.stringify(value));
}

describe("parseClientFrame", () => {
  it("rejects text that is not JSON", () => {
    const result = parseClientFrame("not json at all");
    assert.equal(result.ok, false);
  });

  it("rejects a frame with no discriminator", () => {
    assert.equal(parse({ token: "abc" }).ok, false);
  });

  it("rejects an unknown frame type", () => {
    assert.equal(parse({ t: "teleport" }).ok, false);
  });

  describe("hello", () => {
    it("accepts a minimal hello and defaults the optional fields", () => {
      const result = parse({ t: "hello", token: "abc123" });

      assert.equal(result.ok, true);
      assert.ok(result.ok);
      assert.equal(result.frame.t, "hello");
      if (result.frame.t === "hello") {
        assert.equal(result.frame.version, "unknown");
        assert.equal(result.frame.hidePresence, false);
      }
    });

    it("keeps the supplied version and presence preference", () => {
      const result = parse({
        t: "hello",
        token: "abc123",
        version: "5.2.4",
        hidePresence: true,
      });

      assert.ok(result.ok);
      if (result.frame.t === "hello") {
        assert.equal(result.frame.version, "5.2.4");
        assert.equal(result.frame.hidePresence, true);
      }
    });

    it("rejects an empty token", () => {
      assert.equal(parse({ t: "hello", token: "" }).ok, false);
    });

    it("rejects an implausibly long token", () => {
      assert.equal(parse({ t: "hello", token: "x".repeat(513) }).ok, false);
    });
  });

  describe("join", () => {
    it("accepts a numeric channel id", () => {
      const result = parse({ t: "join", channel: "12345" });
      assert.ok(result.ok);
      if (result.frame.t === "join") {
        assert.equal(result.frame.channel, "12345");
      }
    });

    it("rejects a channel name in place of an id", () => {
      assert.equal(parse({ t: "join", channel: "forsen" }).ok, false);
    });

    it("rejects a numeric channel id sent as a number", () => {
      assert.equal(parse({ t: "join", channel: 12345 }).ok, false);
    });
  });

  describe("say", () => {
    it("accepts a message with a nonce", () => {
      const result = parse({
        t: "say",
        channel: "12345",
        body: "hello",
        nonce: "abc",
      });

      assert.ok(result.ok);
      if (result.frame.t === "say") {
        assert.equal(result.frame.nonce, "abc");
      }
    });

    it("accepts a message without a nonce", () => {
      const result = parse({ t: "say", channel: "12345", body: "hello" });
      assert.ok(result.ok);
      if (result.frame.t === "say") {
        assert.equal(result.frame.nonce, undefined);
      }
    });

    it("rejects an empty body", () => {
      assert.equal(parse({ t: "say", channel: "12345", body: "" }).ok, false);
    });
  });

  describe("typing", () => {
    it("defaults to active", () => {
      const result = parse({ t: "typing", channel: "12345" });
      assert.ok(result.ok);
      if (result.frame.t === "typing") {
        assert.equal(result.frame.active, true);
      }
    });

    it("carries an explicit inactive signal", () => {
      const result = parse({ t: "typing", channel: "12345", active: false });
      assert.ok(result.ok);
      if (result.frame.t === "typing") {
        assert.equal(result.frame.active, false);
      }
    });
  });

  it("accepts a bare heartbeat", () => {
    const result = parse({ t: "beat" });
    assert.ok(result.ok);
    assert.equal(result.frame.t, "beat");
  });
});
