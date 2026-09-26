/**
 * Wire protocol for the realtime socket.
 *
 * Every frame is JSON with a `t` discriminator. Client frames are validated
 * before they reach any handler, so handlers can trust their input.
 */

import { z } from "zod";

/** A Twitch numeric id as a string, which is how Twitch itself reports them. */
const twitchId = z.string().regex(/^\d{1,20}$/, "expected a Twitch numeric id");

const nonce = z.string().min(1).max(64);

export const helloFrame = z.object({
  t: z.literal("hello"),
  token: z.string().min(1).max(512),
  /** Client build string, recorded with presence for support purposes. */
  version: z.string().min(1).max(64).default("unknown"),
  /** When true the user's presence is not published to other viewers. */
  hidePresence: z.boolean().default(false),
});

export const joinFrame = z.object({
  t: z.literal("join"),
  channel: twitchId,
});

export const partFrame = z.object({
  t: z.literal("part"),
  channel: twitchId,
});

export const sayFrame = z.object({
  t: z.literal("say"),
  channel: twitchId,
  body: z.string().min(1),
  nonce: nonce.optional(),
});

export const typingFrame = z.object({
  t: z.literal("typing"),
  channel: twitchId,
  /** False signals the user cleared their input box. */
  active: z.boolean().default(true),
});

export const beatFrame = z.object({
  t: z.literal("beat"),
});

export const clientFrame = z.discriminatedUnion("t", [
  helloFrame,
  joinFrame,
  partFrame,
  sayFrame,
  typingFrame,
  beatFrame,
]);

export type ClientFrame = z.infer<typeof clientFrame>;
export type HelloFrame = z.infer<typeof helloFrame>;

export interface ShadowAuthor {
  id: string;
  login: string;
  displayName: string;
}

export interface ShadowMessage {
  id: number;
  channel: string;
  author: ShadowAuthor;
  body: string;
  sentAt: number;
}

export type ServerFrame =
  | { t: "ready"; user: ShadowAuthor }
  | { t: "joined"; channel: string; history: ShadowMessage[] }
  | { t: "parted"; channel: string }
  | ({ t: "message" } & ShadowMessage)
  | { t: "ack"; nonce: string; id: number }
  | { t: "typing"; channel: string; login: string; active: boolean }
  | {
      t: "restricted";
      channel: string;
      /** Epoch millis the restriction lifts, or null when permanent. */
      until: number | null;
      reason: string;
    }
  | { t: "error"; code: ErrorCode; message: string; nonce?: string };

export type ErrorCode =
  | "bad_frame"
  | "unauthenticated"
  | "auth_failed"
  | "already_authenticated"
  | "not_joined"
  | "restricted"
  | "too_long"
  | "rate_limited"
  | "internal";

/**
 * Parses a raw socket payload. Returns a discriminated result rather than
 * throwing, because a malformed frame is an expected condition on a public
 * socket, not an exceptional one.
 */
export function parseClientFrame(
  raw: string,
): { ok: true; frame: ClientFrame } | { ok: false; reason: string } {
  let json: unknown;
  try {
    json = JSON.parse(raw);
  } catch {
    return { ok: false, reason: "payload was not valid JSON" };
  }

  const result = clientFrame.safeParse(json);
  if (!result.success) {
    return { ok: false, reason: result.error.issues[0]?.message ?? "invalid frame" };
  }

  return { ok: true, frame: result.data };
}
