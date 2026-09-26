/**
 * Connection hub.
 *
 * Owns the set of live sockets, which rooms each has joined, and the routing of
 * frames between them. Storage and policy live in the modules; the hub only
 * decides who hears what.
 *
 * The socket is deliberately transport-shaped rather than tied to a WebSocket
 * implementation, so the whole handshake and routing path is testable without
 * opening a port.
 */

import { AuthError, type Identity, type Validator } from "./auth.ts";
import type { ShadowChat } from "./modules/shadow.ts";
import { ShadowRejection } from "./modules/shadow.ts";
import type { Presence, TypingTracker } from "./modules/presence.ts";
import type { Users } from "./modules/users.ts";
import {
  parseClientFrame,
  type ClientFrame,
  type ErrorCode,
  type ServerFrame,
  type ShadowAuthor,
} from "./protocol.ts";

/** The parts of a socket the hub needs. */
export interface Socket {
  send(payload: string): void;
  close(): void;
}

interface Connection {
  socket: Socket;
  identity: Identity | null;
  /** Channel ids this connection has joined. */
  rooms: Set<string>;
  clientVersion: string;
  hidePresence: boolean;
  /** Timestamps of recent `say` frames, for rate limiting. */
  recentSends: number[];
}

/** No more than this many messages per window, per connection. */
const SEND_LIMIT = 10;
const SEND_WINDOW_MS = 10_000;

export interface HubDependencies {
  validator: Validator;
  users: Users;
  shadow: ShadowChat;
  presence: Presence;
  typing: TypingTracker;
  now?: () => number;
  /** Called for unexpected failures, so the caller decides how to log. */
  onError?: (error: unknown) => void;
}

export class Hub {
  private readonly connections = new Map<Socket, Connection>();
  /** channel id -> the connections currently in that room. */
  private readonly rooms = new Map<string, Set<Connection>>();
  private readonly now: () => number;
  private readonly onError: (error: unknown) => void;

  constructor(private readonly deps: HubDependencies) {
    this.now = deps.now ?? Date.now;
    this.onError = deps.onError ?? (() => {});
  }

  get connectionCount(): number {
    return this.connections.size;
  }

  /** Number of connections in a room, for diagnostics. */
  roomSize(channelId: string): number {
    return this.rooms.get(channelId)?.size ?? 0;
  }

  add(socket: Socket): void {
    this.connections.set(socket, {
      socket,
      identity: null,
      rooms: new Set(),
      clientVersion: "unknown",
      hidePresence: false,
      recentSends: [],
    });
  }

  remove(socket: Socket): void {
    const connection = this.connections.get(socket);
    if (connection === undefined) {
      return;
    }

    for (const channelId of connection.rooms) {
      this.leaveRoom(connection, channelId);
    }

    // Only drop presence when this was the account's last connection: a user
    // with two windows open is still online after closing one.
    if (connection.identity !== null && !this.hasOtherConnection(connection)) {
      this.deps.presence.release(connection.identity.twitchId);
    }

    this.connections.delete(socket);
  }

  /** Handles one raw payload from a socket. */
  async handle(socket: Socket, payload: string): Promise<void> {
    const connection = this.connections.get(socket);
    if (connection === undefined) {
      return;
    }

    const parsed = parseClientFrame(payload);
    if (!parsed.ok) {
      this.fail(connection, "bad_frame", parsed.reason);
      return;
    }

    try {
      await this.dispatch(connection, parsed.frame);
    } catch (error) {
      this.onError(error);
      this.fail(connection, "internal", "the server could not handle that");
    }
  }

  private async dispatch(
    connection: Connection,
    frame: ClientFrame,
  ): Promise<void> {
    if (frame.t === "hello") {
      await this.onHello(connection, frame.token, frame.version, frame.hidePresence);
      return;
    }

    // Everything else requires a known identity.
    if (connection.identity === null) {
      this.fail(connection, "unauthenticated", "send a hello frame first");
      return;
    }

    switch (frame.t) {
      case "join":
        this.onJoin(connection, frame.channel);
        break;
      case "part":
        this.onPart(connection, frame.channel);
        break;
      case "say":
        this.onSay(connection, frame.channel, frame.body, frame.nonce);
        break;
      case "typing":
        this.onTyping(connection, frame.channel, frame.active);
        break;
      case "beat":
        this.onBeat(connection);
        break;
    }
  }

  private async onHello(
    connection: Connection,
    token: string,
    version: string,
    hidePresence: boolean,
  ): Promise<void> {
    if (connection.identity !== null) {
      this.fail(connection, "already_authenticated", "this socket already said hello");
      return;
    }

    let identity: Identity;
    try {
      identity = await this.deps.validator.validate(token);
    } catch (error) {
      if (error instanceof AuthError) {
        this.fail(connection, "auth_failed", error.message);
      } else {
        this.onError(error);
        this.fail(connection, "auth_failed", "could not verify that token");
      }
      // A socket that cannot prove who it is has nothing further to say.
      connection.socket.close();
      return;
    }

    // The socket may have gone away while we were waiting on Twitch.
    if (!this.connections.has(connection.socket)) {
      return;
    }

    connection.identity = identity;
    connection.clientVersion = version;
    connection.hidePresence = hidePresence;

    // Presence and paint selections reference this row, so it must exist first.
    this.deps.users.remember(identity);
    this.deps.presence.beat(identity.twitchId, version, hidePresence);
    this.send(connection, { t: "ready", user: authorOf(identity) });
  }

  private onJoin(connection: Connection, channelId: string): void {
    if (!connection.rooms.has(channelId)) {
      connection.rooms.add(channelId);

      let room = this.rooms.get(channelId);
      if (room === undefined) {
        room = new Set();
        this.rooms.set(channelId, room);
      }
      room.add(connection);
    }

    this.send(connection, {
      t: "joined",
      channel: channelId,
      history: this.deps.shadow.history(channelId),
    });

    // Tell a joiner immediately if they are silenced here, so the client can
    // show it rather than letting them type into a wall.
    const identity = connection.identity;
    if (identity !== null) {
      const restriction = this.deps.shadow.restrictionFor(channelId, identity.twitchId);
      if (restriction !== null) {
        this.send(connection, {
          t: "restricted",
          channel: channelId,
          until: restriction.until,
          reason: restriction.reason,
        });
      }
    }
  }

  private onPart(connection: Connection, channelId: string): void {
    this.leaveRoom(connection, channelId);
    connection.rooms.delete(channelId);
    this.send(connection, { t: "parted", channel: channelId });
  }

  private onSay(
    connection: Connection,
    channelId: string,
    body: string,
    nonce: string | undefined,
  ): void {
    const identity = connection.identity;
    if (identity === null) {
      return;
    }

    if (!connection.rooms.has(channelId)) {
      this.fail(connection, "not_joined", "join the channel first", nonce);
      return;
    }

    if (!this.withinRateLimit(connection)) {
      this.fail(connection, "rate_limited", "you are sending too quickly", nonce);
      return;
    }

    let message;
    try {
      message = this.deps.shadow.post(channelId, authorOf(identity), body);
    } catch (error) {
      if (error instanceof ShadowRejection) {
        this.fail(connection, error.code, error.message, nonce);

        // Refresh the client's view of its own restriction, in case a
        // moderator acted while it was not looking.
        if (error.code === "restricted") {
          const restriction = this.deps.shadow.restrictionFor(
            channelId,
            identity.twitchId,
          );
          if (restriction !== null) {
            this.send(connection, {
              t: "restricted",
              channel: channelId,
              until: restriction.until,
              reason: restriction.reason,
            });
          }
        }
        return;
      }
      throw error;
    }

    if (nonce !== undefined) {
      this.send(connection, { t: "ack", nonce, id: message.id });
    }

    this.broadcast(channelId, { t: "message", ...message });

    // Sending clears any typing indicator, so it does not linger.
    this.deps.typing.set(channelId, identity.login, false);
    this.broadcast(
      channelId,
      { t: "typing", channel: channelId, login: identity.login, active: false },
      connection,
    );
  }

  private onTyping(connection: Connection, channelId: string, active: boolean): void {
    const identity = connection.identity;
    if (identity === null || !connection.rooms.has(channelId)) {
      return;
    }

    this.deps.typing.set(channelId, identity.login, active);
    this.broadcast(
      channelId,
      { t: "typing", channel: channelId, login: identity.login, active },
      connection,
    );
  }

  private onBeat(connection: Connection): void {
    const identity = connection.identity;
    if (identity === null) {
      return;
    }

    this.deps.presence.beat(
      identity.twitchId,
      connection.clientVersion,
      connection.hidePresence,
    );
  }

  /**
   * Pushes a restriction to a user's own sockets, so a timeout issued elsewhere
   * shows up without waiting for them to try to speak.
   */
  notifyRestricted(
    channelId: string,
    twitchId: string,
    until: number | null,
    reason: string,
  ): void {
    for (const connection of this.connections.values()) {
      if (connection.identity?.twitchId !== twitchId) {
        continue;
      }
      if (!connection.rooms.has(channelId)) {
        continue;
      }
      this.send(connection, { t: "restricted", channel: channelId, until, reason });
    }
  }

  private broadcast(
    channelId: string,
    frame: ServerFrame,
    except?: Connection,
  ): void {
    const room = this.rooms.get(channelId);
    if (room === undefined) {
      return;
    }

    const payload = JSON.stringify(frame);
    for (const connection of room) {
      if (connection === except) {
        continue;
      }
      this.write(connection, payload);
    }
  }

  private leaveRoom(connection: Connection, channelId: string): void {
    const room = this.rooms.get(channelId);
    if (room === undefined) {
      return;
    }

    room.delete(connection);
    if (room.size === 0) {
      this.rooms.delete(channelId);
    }

    const login = connection.identity?.login;
    if (login !== undefined) {
      this.deps.typing.set(channelId, login, false);
    }
  }

  private hasOtherConnection(connection: Connection): boolean {
    const twitchId = connection.identity?.twitchId;
    if (twitchId === undefined) {
      return false;
    }

    for (const other of this.connections.values()) {
      if (other !== connection && other.identity?.twitchId === twitchId) {
        return true;
      }
    }
    return false;
  }

  private withinRateLimit(connection: Connection): boolean {
    const cutoff = this.now() - SEND_WINDOW_MS;
    connection.recentSends = connection.recentSends.filter(
      (timestamp) => timestamp > cutoff,
    );

    if (connection.recentSends.length >= SEND_LIMIT) {
      return false;
    }

    connection.recentSends.push(this.now());
    return true;
  }

  private fail(
    connection: Connection,
    code: ErrorCode,
    message: string,
    nonce?: string,
  ): void {
    const frame: ServerFrame =
      nonce === undefined
        ? { t: "error", code, message }
        : { t: "error", code, message, nonce };
    this.send(connection, frame);
  }

  private send(connection: Connection, frame: ServerFrame): void {
    this.write(connection, JSON.stringify(frame));
  }

  private write(connection: Connection, payload: string): void {
    try {
      connection.socket.send(payload);
    } catch (error) {
      // A write failure means the peer is gone; drop it rather than letting the
      // failure escape into an unrelated request's error path.
      this.onError(error);
      this.remove(connection.socket);
    }
  }
}

function authorOf(identity: Identity): ShadowAuthor {
  return {
    id: identity.twitchId,
    login: identity.login,
    displayName: identity.displayName,
  };
}
