/**
 * HTTP API.
 *
 * Request-shaped features live here; anything realtime goes over the socket in
 * hub.ts. Every route but /health requires a Twitch token, and the routes that
 * change a channel's state additionally require that the caller moderates it.
 */

import cors from "@fastify/cors";
import Fastify, { type FastifyInstance, type FastifyRequest } from "fastify";
import { z } from "zod";
import { AuthError, type Identity, type Validator } from "./auth.ts";
import type { Config } from "./config.ts";
import type { Hub } from "./hub.ts";
import type { GlobalBans } from "./modules/globalBans.ts";
import { ModerationError, type ModerationLookup } from "./modules/moderation.ts";
import { PaintRejection, type Paints } from "./modules/paints.ts";
import type { Presence } from "./modules/presence.ts";
import type { ShadowChat } from "./modules/shadow.ts";
import type { Users } from "./modules/users.ts";

export interface ApiDependencies {
  config: Config;
  validator: Validator;
  moderation: ModerationLookup;
  users: Users;
  bans: GlobalBans;
  paints: Paints;
  presence: Presence;
  shadow: ShadowChat;
  hub: Hub;
}

const twitchId = z.string().regex(/^\d{1,20}$/);

const recordBanBody = z.object({
  offenderId: twitchId,
  channelId: twitchId,
  channelLogin: z.string().min(1).max(64),
  reason: z.string().max(500).default(""),
  context: z
    .array(z.object({ body: z.string().max(500), sentAt: z.number().int() }))
    .max(50)
    .default([]),
});

const restrictBody = z.object({
  userId: twitchId,
  /** Null means a permanent ban. */
  durationMs: z.number().int().positive().max(1000 * 60 * 60 * 24 * 14).nullable(),
  reason: z.string().max(500).default(""),
});

const createPaintBody = z.object({
  name: z.string(),
  definition: z.unknown(),
});

const updatePaintBody = z.object({
  name: z.string().optional(),
  shared: z.boolean().optional(),
});

const wearBody = z.object({
  paintId: z.string().min(1).max(64).nullable(),
});

/** Splits a comma-separated `ids` query parameter, bounded to keep queries cheap. */
function parseIds(raw: unknown): string[] {
  if (typeof raw !== "string" || raw === "") {
    return [];
  }
  return raw
    .split(",")
    .map((value) => value.trim())
    .filter((value) => /^\d{1,20}$/.test(value))
    .slice(0, 200);
}

export async function buildApi(deps: ApiDependencies): Promise<FastifyInstance> {
  const app = Fastify({ logger: false, bodyLimit: 256 * 1024 });

  // A server you cannot watch is hard to develop against: when a client feature
  // shows nothing, the first thing worth knowing is whether it asked at all,
  // and silence here is indistinguishable from a request that never arrived.
  // Off by default, because this prints one line per request.
  if (process.env.LOG_REQUESTS === "1") {
    app.addHook("onResponse", async (request, reply) => {
      console.log(
        "%s %s -> %d",
        request.method,
        request.url,
        reply.statusCode,
      );
    });
  }

  await app.register(cors, {
    origin: deps.config.allowedOrigins.includes("*")
      ? true
      : deps.config.allowedOrigins,
  });

  /** Resolves the caller, or replies 401 and returns null. */
  async function authenticate(request: FastifyRequest): Promise<
    { identity: Identity; token: string } | null
  > {
    const header = request.headers.authorization;
    if (typeof header !== "string" || header === "") {
      return null;
    }

    const identity = await deps.validator.validate(header);
    return { identity, token: header.replace(/^(OAuth|Bearer)\s+/i, "").trim() };
  }

  app.setErrorHandler((error, _request, reply) => {
    if (error instanceof AuthError || error instanceof ModerationError) {
      return reply.code(error.statusCode).send({ error: error.message });
    }
    if (error instanceof PaintRejection) {
      return reply.code(error.statusCode).send({ error: error.message });
    }
    if (error instanceof z.ZodError) {
      return reply
        .code(400)
        .send({ error: error.issues[0]?.message ?? "invalid request" });
    }
    // Fastify's own validation and parse failures carry a usable status. The
    // instanceof chain above narrows `error` away from its declared type, so
    // read the remaining fields through an explicit shape.
    const fallback = error as { statusCode?: unknown; message?: unknown };
    const status =
      typeof fallback.statusCode === "number" ? fallback.statusCode : 500;

    // A 500 is our bug, so do not leak its message to the caller.
    if (status === 500) {
      return reply.code(500).send({ error: "internal error" });
    }
    return reply.code(status).send({
      error: typeof fallback.message === "string" ? fallback.message : "request failed",
    });
  });

  app.get("/health", async () => ({
    ok: true,
    connections: deps.hub.connectionCount,
  }));

  // --- ban registry ------------------------------------------------------

  app.get("/v1/bans/:offenderId", async (request, reply) => {
    const caller = await authenticate(request);
    if (caller === null) {
      return reply.code(401).send({ error: "authentication required" });
    }

    const { offenderId } = request.params as { offenderId: string };
    if (!twitchId.safeParse(offenderId).success) {
      return reply.code(400).send({ error: "offenderId must be a Twitch id" });
    }

    const viewing = (request.query as { channel?: string }).channel ?? "";

    return {
      offenderId,
      markerCount: deps.bans.markerCount(offenderId, viewing),
      history: deps.bans.historyFor(offenderId),
      activeChannels: deps.bans.activeChannelsFor(offenderId),
    };
  });

  app.get("/v1/bans", async (request, reply) => {
    const caller = await authenticate(request);
    if (caller === null) {
      return reply.code(401).send({ error: "authentication required" });
    }

    const query = request.query as { ids?: string; channel?: string };
    const ids = parseIds(query.ids);
    const counts = deps.bans.markerCounts(ids, query.channel ?? "");

    return { markers: Object.fromEntries(counts) };
  });

  app.post("/v1/bans", async (request, reply) => {
    const caller = await authenticate(request);
    if (caller === null) {
      return reply.code(401).send({ error: "authentication required" });
    }

    const body = recordBanBody.parse(request.body);

    // Only a channel's own moderators may add to its ban record, or anyone
    // could poison the registry with bans that never happened.
    const allowed = await deps.moderation.moderates(
      caller.token,
      caller.identity.twitchId,
      body.channelId,
    );
    if (!allowed) {
      return reply.code(403).send({ error: "you do not moderate that channel" });
    }

    return reply.code(201).send(deps.bans.record(body));
  });

  app.post("/v1/bans/:offenderId/clear", async (request, reply) => {
    const caller = await authenticate(request);
    if (caller === null) {
      return reply.code(401).send({ error: "authentication required" });
    }

    const { offenderId } = request.params as { offenderId: string };
    const { channelId } = z
      .object({ channelId: twitchId })
      .parse(request.body);

    const allowed = await deps.moderation.moderates(
      caller.token,
      caller.identity.twitchId,
      channelId,
    );
    if (!allowed) {
      return reply.code(403).send({ error: "you do not moderate that channel" });
    }

    const changed = deps.bans.clear(offenderId, channelId, caller.identity.twitchId);
    return { changed };
  });

  app.delete("/v1/bans/:offenderId/clear", async (request, reply) => {
    const caller = await authenticate(request);
    if (caller === null) {
      return reply.code(401).send({ error: "authentication required" });
    }

    const { offenderId } = request.params as { offenderId: string };
    const { channelId } = z.object({ channelId: twitchId }).parse(request.body);

    const allowed = await deps.moderation.moderates(
      caller.token,
      caller.identity.twitchId,
      channelId,
    );
    if (!allowed) {
      return reply.code(403).send({ error: "you do not moderate that channel" });
    }

    return { changed: deps.bans.unclear(offenderId, channelId) };
  });

  // --- shadow chat moderation -------------------------------------------

  app.post("/v1/shadow/:channelId/restrict", async (request, reply) => {
    const caller = await authenticate(request);
    if (caller === null) {
      return reply.code(401).send({ error: "authentication required" });
    }

    const { channelId } = request.params as { channelId: string };
    if (!twitchId.safeParse(channelId).success) {
      return reply.code(400).send({ error: "channelId must be a Twitch id" });
    }

    const body = restrictBody.parse(request.body);

    const allowed = await deps.moderation.moderates(
      caller.token,
      caller.identity.twitchId,
      channelId,
    );
    if (!allowed) {
      return reply.code(403).send({ error: "you do not moderate that channel" });
    }

    const restriction = deps.shadow.restrict(
      channelId,
      body.userId,
      caller.identity.twitchId,
      body.durationMs,
      body.reason,
    );

    // Tell the affected user now, rather than when they next try to speak.
    deps.hub.notifyRestricted(
      channelId,
      body.userId,
      restriction.until,
      restriction.reason,
    );

    return restriction;
  });

  app.delete("/v1/shadow/:channelId/restrict/:userId", async (request, reply) => {
    const caller = await authenticate(request);
    if (caller === null) {
      return reply.code(401).send({ error: "authentication required" });
    }

    const { channelId, userId } = request.params as {
      channelId: string;
      userId: string;
    };

    const allowed = await deps.moderation.moderates(
      caller.token,
      caller.identity.twitchId,
      channelId,
    );
    if (!allowed) {
      return reply.code(403).send({ error: "you do not moderate that channel" });
    }

    deps.shadow.lift(channelId, userId);
    return { lifted: true };
  });

  // --- presence ----------------------------------------------------------

  app.get("/v1/presence", async (request, reply) => {
    const caller = await authenticate(request);
    if (caller === null) {
      return reply.code(401).send({ error: "authentication required" });
    }

    const ids = parseIds((request.query as { ids?: string }).ids);
    return { presence: Object.fromEntries(deps.presence.statesOf(ids)) };
  });

  // --- paints ------------------------------------------------------------

  app.get("/v1/paints/library", async (request, reply) => {
    const caller = await authenticate(request);
    if (caller === null) {
      return reply.code(401).send({ error: "authentication required" });
    }

    const query = request.query as { limit?: string; offset?: string };
    const limit = Math.min(Number.parseInt(query.limit ?? "100", 10) || 100, 200);
    const offset = Math.max(Number.parseInt(query.offset ?? "0", 10) || 0, 0);

    return { paints: deps.paints.library(limit, offset) };
  });

  app.get("/v1/paints/mine", async (request, reply) => {
    const caller = await authenticate(request);
    if (caller === null) {
      return reply.code(401).send({ error: "authentication required" });
    }

    return { paints: deps.paints.byAuthor(caller.identity.twitchId) };
  });

  app.post("/v1/paints", async (request, reply) => {
    const caller = await authenticate(request);
    if (caller === null) {
      return reply.code(401).send({ error: "authentication required" });
    }

    const body = createPaintBody.parse(request.body);
    deps.users.remember(caller.identity);

    const paint = deps.paints.create(
      caller.identity.twitchId,
      body.name,
      body.definition,
    );
    return reply.code(201).send(paint);
  });

  app.patch("/v1/paints/:paintId", async (request, reply) => {
    const caller = await authenticate(request);
    if (caller === null) {
      return reply.code(401).send({ error: "authentication required" });
    }

    const { paintId } = request.params as { paintId: string };
    const body = updatePaintBody.parse(request.body);

    let paint = deps.paints.get(paintId);
    if (paint === null) {
      return reply.code(404).send({ error: "no such paint" });
    }

    if (body.name !== undefined) {
      paint = deps.paints.rename(paintId, caller.identity.twitchId, body.name);
    }
    if (body.shared !== undefined) {
      paint = deps.paints.setShared(paintId, caller.identity.twitchId, body.shared);
    }

    return paint;
  });

  app.delete("/v1/paints/:paintId", async (request, reply) => {
    const caller = await authenticate(request);
    if (caller === null) {
      return reply.code(401).send({ error: "authentication required" });
    }

    const { paintId } = request.params as { paintId: string };
    deps.paints.remove(paintId, caller.identity.twitchId);

    return { deleted: true };
  });

  app.put("/v1/paints/worn", async (request, reply) => {
    const caller = await authenticate(request);
    if (caller === null) {
      return reply.code(401).send({ error: "authentication required" });
    }

    const body = wearBody.parse(request.body);
    deps.users.remember(caller.identity);
    deps.paints.wear(caller.identity.twitchId, body.paintId);

    return { paint: deps.paints.wornBy(caller.identity.twitchId) };
  });

  app.get("/v1/paints/worn", async (request, reply) => {
    const caller = await authenticate(request);
    if (caller === null) {
      return reply.code(401).send({ error: "authentication required" });
    }

    const ids = parseIds((request.query as { ids?: string }).ids);
    const worn = deps.paints.wornByMany(ids);

    return { worn: Object.fromEntries(worn) };
  });

  return app;
}
