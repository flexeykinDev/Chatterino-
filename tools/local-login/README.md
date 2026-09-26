# Signing in against your own Twitch application

Upstream Chatterino logs in through `chatterino.com/client_login`, which hands
back a token minted for *its* client id. A fork should not use that. The token
is tied to an application whose scopes, rate limits and reputation belong to
someone else, and the companion service in [`server/`](../../server) refuses
tokens issued to a different application once `TWITCH_CLIENT_ID` is set.

What "logging in" actually means here is small. Chatterino stores four values
per account:

| | |
| --- | --- |
| `username` | your login name |
| `userID` | your numeric Twitch id |
| `clientID` | the application the token was issued to |
| `oauthToken` | the token itself |

Anything that produces those four gets you signed in. There is a form for
typing them by hand in **Settings → Accounts → Add user → Advanced**.

## Register the application

1. Go to <https://dev.twitch.tv/console/apps> and **Register Your Application**.
2. **OAuth Redirect URLs**: `http://localhost:7777`
   Twitch allows plain `http` for `localhost` and nowhere else. It must match
   exactly — a trailing slash is a different URL as far as Twitch is concerned.

   **Do not press "Add" to create a second row.** A blank redirect field fails
   the console's validation, which then reports *"Redirection URLs must use the
   HTTPS protocol"* — pointing at the scheme, under a field that has no scheme
   in it, while the `http://localhost` URL it appears to be about is perfectly
   acceptable. Leave exactly one row filled in and press Create. This is a bug
   in the console rather than a rule: Twitch's own
   [Get Started guide](https://dev.twitch.tv/docs/api/get-started/) still tells
   you to use `http://localhost:3000`, and a staff reply confirms the blank
   field is the cause —
   [discuss.dev.twitch.com](https://discuss.dev.twitch.com/t/unable-to-use-localhost-are-redirect-url-for-oauth-implicit-grant-flow/61951).
3. **Category**: Chat Bot.
4. **Client Type**: **Public**.
   This matters. A Confidential client cannot use the implicit grant, and Twitch
   rejects the authorise request with an error that does not say so plainly.
5. Copy the **Client ID**. There is no need for a client secret; nothing here
   uses one.

## Get the four values

```sh
node tools/local-login/login.mjs --client-id <your client id>
```

It prints an authorise URL. Open it, approve, and the page it redirects to shows
the four values with a button that copies them in the format Chatterino's
**Paste login info** button expects.

Then, in Chatterino: **Settings → Accounts → Add user → Paste login info**. Or
switch to **Advanced** and type the four values in.

Some notes on what it does:

- **The scopes come from `src/providers/twitch/TwitchAccountManager.cpp`**, read
  at run time. Asking for a scope the client does not use is harmless; missing
  one is not, because the feature needing it fails much later and looks like a
  bug in that feature. If a scope is not granted, the tool says which.
- **The token never leaves your machine.** The implicit grant returns it in the
  URL fragment, which browsers do not send to servers, so the page reads it in
  JavaScript and posts it back to the local process. Nothing is written to disk
  and the terminal only ever prints a masked form.
- **The client id is taken from Twitch's answer**, not from the argument, so a
  token cannot end up paired with the wrong id and fail later for no visible
  reason.
- Use `--port` if 7777 is taken, and register the matching redirect URL.

## Revoking

Disconnect the application under **Settings → Connections** on Twitch, which
invalidates every token it issued you. Tokens from the implicit grant last about
60 days and cannot be refreshed; run the tool again.
