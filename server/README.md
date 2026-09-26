# Fork backend services

Server-side companion to the Chatterino fork. The desktop client works without
it — every feature that depends on this service degrades to "off" when the
server is unreachable — but the following features cannot exist without it,
because they are inherently shared state between users:

| Feature | Why it needs a server |
| --- | --- |
| Shadow chat | Messages bypass Twitch entirely, so something must relay them. |
| Global ban registry | A cross-channel ban history is one shared database. |
| Presence badge | "Is this person running the client right now?" is other users' state. |
| Moderator typing indicator | Broadcast between different people's clients. |
| Paint library | User-authored paints are shared, so they need shared storage. |

## Running it

```sh
npm install
npm test          # compiles, then runs the suite
npm run build
TWITCH_CLIENT_ID=<your app id> npm start
```

### Configuration

All settings come from the environment. Defaults suit local development.

| Variable | Default | Meaning |
| --- | --- | --- |
| `PORT` | `8080` | Listen port. |
| `HOST` | `127.0.0.1` | Bind address. Use `0.0.0.0` behind a reverse proxy. |
| `DATABASE_PATH` | `./data/server.sqlite` | SQLite file. |
| `TWITCH_CLIENT_ID` | *(empty)* | Your Twitch app id. **Set this in production** — see below. |
| `ALLOWED_ORIGINS` | `*` | Comma-separated origin allowlist for the HTTP API. |
| `TOKEN_CACHE_TTL_MS` | `300000` | How long a validated token stays cached. |
| `PRESENCE_TIMEOUT_MS` | `90000` | Heartbeat age past which a session reads as offline. |
| `MAX_SHADOW_MESSAGE_LENGTH` | `500` | Longest accepted shadow chat message. |

Leaving `TWITCH_CLIENT_ID` empty disables the check that a token was issued to
*your* application. That is convenient in development and unsafe in production:
without it, a token minted for any other Twitch app is accepted, letting that
app's users act as yours.

## Authentication

The client never sends a password. It sends the OAuth token it already holds for
Twitch, and the server asks Twitch's `/oauth2/validate` endpoint who it belongs
to. Successful validations are cached for `TOKEN_CACHE_TTL_MS` so that a
reconnect storm does not become a validation storm.

The server therefore needs no user database of its own and stores no
credentials — only the Twitch id, login and display name of accounts it has
seen.

## Realtime protocol

One WebSocket carries shadow chat, presence heartbeats and typing indicators.
Every frame is a JSON object with a `t` discriminator.

### Client to server

| Frame | Fields | Notes |
| --- | --- | --- |
| `hello` | `token`, `version?`, `hidePresence?` | Must be the first frame. |
| `join` | `channel` | Channel **id**, not name. |
| `part` | `channel` | |
| `say` | `channel`, `body`, `nonce?` | `nonce` is echoed in the `ack`. |
| `typing` | `channel`, `active?` | `active: false` when the input box empties. |
| `beat` | — | Presence heartbeat. |

### Server to client

| Frame | Fields |
| --- | --- |
| `ready` | `user` |
| `joined` | `channel`, `history` |
| `parted` | `channel` |
| `message` | `id`, `channel`, `author`, `body`, `sentAt` |
| `ack` | `nonce`, `id` |
| `typing` | `channel`, `login`, `active` |
| `restricted` | `channel`, `until`, `reason` |
| `error` | `code`, `message`, `nonce?` |

`restricted.until` is epoch millis, or `null` for a permanent ban.

Error codes: `bad_frame`, `unauthenticated`, `auth_failed`,
`already_authenticated`, `not_joined`, `restricted`, `too_long`,
`rate_limited`, `internal`.

## Moderation

Shadow chat is not a way around a channel's moderators. A channel's moderators
can time out or ban a user in its shadow room exactly as they can in the real
one, and those restrictions are enforced server-side on every message. A
restriction is scoped to a single channel; it never follows the user elsewhere.

## Layout

```
src/
  config.ts             environment parsing
  db.ts                 SQLite schema and forward-only migrations
  auth.ts               Twitch token validation and caching
  protocol.ts           wire frames and their validation
  modules/
    shadow.ts           shadow chat storage and restrictions
    presence.ts         presence states and typing indicators
    globalBans.ts       cross-channel ban registry
test/                   one file per module
```

## The ban registry

A marker next to someone's name means "this person was banned elsewhere" — it is
a prompt to look, not a verdict. Three rules keep it honest:

- **A moderator can vouch.** `clear()` hides the marker for everyone watching
  that channel, and records which moderator decided that.
- **Lifted bans stop counting.** A background pass re-checks old bans; one that
  no longer holds on Twitch is marked lifted and drops out of the marker, while
  staying visible in the history so the record is intact.
- **Your own channel is excluded.** A ban on the channel you are watching does
  not contribute, because that chat can already see it.

Re-banning someone overrides both a previous vouch and a previous lift: the ban
is in force again, and the channel that vouched has evidently changed its mind.

## Status

Implemented and tested: configuration, schema and migrations, Twitch token
validation, frame validation, shadow chat storage and moderation, presence
states, typing indicators, and the cross-channel ban registry with its relay and
revalidation support.

Not yet implemented: the HTTP and WebSocket entry point wiring these together,
and the paint library.
