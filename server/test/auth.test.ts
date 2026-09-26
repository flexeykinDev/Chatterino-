import assert from "node:assert/strict";
import { describe, it } from "node:test";
import { AuthError, normaliseToken, parseIdentity } from "../src/auth.ts";

const CLIENT_ID = "our-client-id";

function validBody(overrides: Record<string, unknown> = {}) {
  return {
    client_id: CLIENT_ID,
    login: "alice",
    user_id: "1234",
    scopes: ["chat:read", "moderator:manage:banned_users"],
    ...overrides,
  };
}

describe("normaliseToken", () => {
  it("leaves a bare token alone", () => {
    assert.equal(normaliseToken("abc123"), "abc123");
  });

  it("strips an OAuth prefix", () => {
    assert.equal(normaliseToken("OAuth abc123"), "abc123");
  });

  it("strips a Bearer prefix regardless of case", () => {
    assert.equal(normaliseToken("bearer abc123"), "abc123");
  });

  it("trims surrounding whitespace", () => {
    assert.equal(normaliseToken("  abc123  "), "abc123");
  });
});

describe("parseIdentity", () => {
  it("extracts the identity from a well-formed response", () => {
    const identity = parseIdentity(validBody(), CLIENT_ID);

    assert.equal(identity.twitchId, "1234");
    assert.equal(identity.login, "alice");
    assert.equal(identity.displayName, "alice");
    assert.deepEqual(identity.scopes, [
      "chat:read",
      "moderator:manage:banned_users",
    ]);
  });

  it("rejects a token minted for another application", () => {
    assert.throws(
      () => parseIdentity(validBody({ client_id: "someone-elses-app" }), CLIENT_ID),
      (error: unknown) => error instanceof AuthError && error.statusCode === 403,
    );
  });

  it("skips the client id check when none is configured", () => {
    const identity = parseIdentity(validBody({ client_id: "anything" }), "");
    assert.equal(identity.twitchId, "1234");
  });

  it("rejects a response with no user id", () => {
    assert.throws(
      () => parseIdentity(validBody({ user_id: undefined }), CLIENT_ID),
      AuthError,
    );
  });

  it("rejects a response with an empty user id", () => {
    assert.throws(() => parseIdentity(validBody({ user_id: "" }), CLIENT_ID), AuthError);
  });

  it("rejects a numeric user id, which would break id comparisons", () => {
    assert.throws(() => parseIdentity(validBody({ user_id: 1234 }), CLIENT_ID), AuthError);
  });

  it("rejects a response with no login", () => {
    assert.throws(
      () => parseIdentity(validBody({ login: undefined }), CLIENT_ID),
      AuthError,
    );
  });

  it("rejects a non-object body", () => {
    assert.throws(() => parseIdentity("nope", CLIENT_ID), AuthError);
    assert.throws(() => parseIdentity(null, CLIENT_ID), AuthError);
  });

  it("tolerates a missing scopes list", () => {
    const identity = parseIdentity(validBody({ scopes: undefined }), CLIENT_ID);
    assert.deepEqual(identity.scopes, []);
  });

  it("drops non-string entries from the scopes list", () => {
    const identity = parseIdentity(validBody({ scopes: ["chat:read", 7, null] }), CLIENT_ID);
    assert.deepEqual(identity.scopes, ["chat:read"]);
  });
});
