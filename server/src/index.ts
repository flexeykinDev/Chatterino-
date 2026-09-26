/**
 * Composition root. Builds every module, wires the socket route onto the HTTP
 * server and starts listening.
 */

import websocket from "@fastify/websocket";
import { TwitchValidator } from "./auth.ts";
import { loadConfig } from "./config.ts";
import { openDatabase } from "./db.ts";
import { buildApi } from "./http.ts";
import { Hub, type Socket } from "./hub.ts";
import { GlobalBans } from "./modules/globalBans.ts";
import { TwitchModeration } from "./modules/moderation.ts";
import { Paints } from "./modules/paints.ts";
import { Presence, TypingTracker } from "./modules/presence.ts";
import { ShadowChat } from "./modules/shadow.ts";
import { Users } from "./modules/users.ts";

async function main(): Promise<void> {
  const config = loadConfig();

  if (config.twitchClientId === "") {
    console.warn(
      "TWITCH_CLIENT_ID is not set: tokens issued to other applications will " +
        "be accepted. Do not run this way in production.",
    );
  }

  const db = openDatabase(config.databasePath);

  const validator = new TwitchValidator(config);
  const moderation = new TwitchModeration(config.twitchClientId);
  const users = new Users(db);
  const shadow = new ShadowChat(db, config.maxShadowMessageLength);
  const presence = new Presence(db, config.presenceTimeoutMs);
  const typing = new TypingTracker();
  const bans = new GlobalBans(db);
  const paints = new Paints(db);

  const hub = new Hub({
    validator,
    users,
    shadow,
    presence,
    typing,
    onError: (error) => {
      console.error("hub error:", error);
    },
  });

  const app = await buildApi({
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

  await app.register(websocket, {
    options: { maxPayload: 64 * 1024 },
  });

  app.get("/socket", { websocket: true }, (connection) => {
    // Adapt the WebSocket to the narrow interface the hub expects, so the hub
    // stays testable without a transport.
    const socket: Socket = {
      send: (payload) => connection.send(payload),
      close: () => connection.close(),
    };

    hub.add(socket);

    connection.on("message", (raw: Buffer) => {
      void hub.handle(socket, raw.toString("utf8"));
    });

    connection.on("close", () => {
      hub.remove(socket);
    });

    connection.on("error", (error: unknown) => {
      console.error("socket error:", error);
      hub.remove(socket);
    });
  });

  const shutdown = async (signal: string): Promise<void> => {
    console.log(`received ${signal}, shutting down`);
    try {
      await app.close();
      db.close();
    } finally {
      process.exit(0);
    }
  };

  process.on("SIGINT", () => void shutdown("SIGINT"));
  process.on("SIGTERM", () => void shutdown("SIGTERM"));

  await app.listen({ port: config.port, host: config.host });
  console.log(`listening on ${config.host}:${config.port}`);
}

main().catch((error: unknown) => {
  console.error("failed to start:", error);
  process.exit(1);
});
