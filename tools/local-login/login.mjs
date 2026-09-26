#!/usr/bin/env node
// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

/**
 * Signs this fork in against your own Twitch application, with no third-party
 * website involved.
 *
 * Chatterino stores exactly four things per account — username, user id, client
 * id and OAuth token — so "logging in" is only a matter of obtaining those four
 * values. Upstream gets them from chatterino.com, which hands out a token minted
 * for *its* client id. A fork cannot use that: the token is tied to an
 * application whose scopes and rate limits belong to someone else, and the
 * companion service refuses tokens minted for another application when
 * TWITCH_CLIENT_ID is set.
 *
 * So this does the same job locally. It serves a page on 127.0.0.1, sends you
 * to Twitch, and turns the token Twitch hands back into the four values.
 *
 * The token never leaves your machine: the implicit grant returns it in the URL
 * fragment, which browsers do not send to servers, so the page reads it in
 * JavaScript and posts it straight back to this process. Nothing is written to
 * disk and nothing is logged in full.
 *
 *   node tools/local-login/login.mjs --client-id <your app's client id>
 *
 * See README.md beside this file for registering the application.
 */

import { createServer } from "node:http";
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const here = dirname(fileURLToPath(import.meta.url));
const repoRoot = join(here, "..", "..");

/** Where the scope list actually lives, so this cannot drift from the client. */
const SCOPE_SOURCE = join(
    repoRoot,
    "src",
    "providers",
    "twitch",
    "TwitchAccountManager.cpp",
);

function parseArgs(argv) {
    const args = { port: 7777, clientId: process.env.TWITCH_CLIENT_ID ?? "" };

    for (let i = 0; i < argv.length; i++) {
        const arg = argv[i];
        if (arg === "--client-id") {
            args.clientId = argv[++i] ?? "";
        } else if (arg === "--port") {
            args.port = Number(argv[++i]);
        } else if (arg === "--help" || arg === "-h") {
            args.help = true;
        } else {
            throw new Error(`unknown argument: ${arg}`);
        }
    }

    return args;
}

/**
 * Reads AUTH_SCOPES out of the client's own source.
 *
 * Asking Twitch for a scope the client does not use is harmless; *missing* one
 * is not — the feature that needs it fails later, at a distance, in a way that
 * looks like a bug in the feature. Reading them here means adding a scope to
 * the client is enough, with nothing to remember to update.
 */
function readScopes() {
    const source = readFileSync(SCOPE_SOURCE, "utf8");
    const start = source.indexOf("const std::vector<QStringView> AUTH_SCOPES{");
    if (start < 0) {
        throw new Error(
            `could not find AUTH_SCOPES in ${SCOPE_SOURCE} — has it moved?`,
        );
    }

    const block = source.slice(start, source.indexOf("\n};", start));
    const scopes = [...block.matchAll(/u"([^"]+)"/g)].map((m) => m[1]);
    if (scopes.length === 0) {
        throw new Error("AUTH_SCOPES was found but looks empty");
    }

    return scopes;
}

/** Shows enough of a token to tell two apart, and not enough to use one. */
function mask(token) {
    if (token.length <= 8) {
        return "*".repeat(token.length);
    }
    return `${token.slice(0, 4)}…${token.slice(-4)} (${token.length} chars)`;
}

const page = (redirectUri) => `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>Chatterino login</title>
<style>
  :root { color-scheme: dark light; }
  body { font: 15px/1.5 system-ui, sans-serif; margin: 0; padding: 2rem;
         display: flex; justify-content: center; }
  main { max-width: 34rem; width: 100%; }
  h1 { font-size: 1.3rem; margin: 0 0 1rem; }
  dl { display: grid; grid-template-columns: max-content 1fr; gap: .4rem 1rem; }
  dt { opacity: .7; }
  dd { margin: 0; font-family: ui-monospace, monospace; word-break: break-all; }
  button { font: inherit; padding: .5rem 1rem; margin-top: 1.5rem;
           border-radius: .4rem; border: 1px solid currentColor;
           background: transparent; color: inherit; cursor: pointer; }
  .err { color: #c0392b; }
  .hint { opacity: .7; margin-top: 1.5rem; }
</style>
</head>
<body><main>
<h1>Chatterino login</h1>
<div id="out">Reading what Twitch sent back…</div>
</main>
<script>
const out = document.getElementById("out");

function show(html) { out.innerHTML = html; }

const hash = new URLSearchParams(location.hash.slice(1));
const token = hash.get("access_token");
const error = hash.get("error_description") || hash.get("error");

if (error) {
  show('<p class="err">Twitch refused: ' + error + "</p>");
} else if (!token) {
  show("<p>Nothing from Twitch in this URL. Open the authorise link printed " +
       "in the terminal.</p>");
} else {
  // Straight back to the local process; the fragment never reaches any server
  // on its own.
  fetch("/token", {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify({ token }),
  })
    .then((r) => r.json())
    .then((data) => {
      if (data.error) {
        show('<p class="err">' + data.error + "</p>");
        return;
      }

      history.replaceState(null, "", ${JSON.stringify(redirectUri)});

      show(
        "<dl>" +
        "<dt>Username</dt><dd>" + data.username + "</dd>" +
        "<dt>User ID</dt><dd>" + data.userId + "</dd>" +
        "<dt>Client ID</dt><dd>" + data.clientId + "</dd>" +
        "<dt>OAuth token</dt><dd>" + data.token + "</dd>" +
        "</dl>" +
        '<button id="copy">Copy for &ldquo;Paste login info&rdquo;</button>' +
        '<p class="hint">In Chatterino: Settings &rarr; Accounts &rarr; Add ' +
        "user. Press that button, then <b>Paste login info</b>. Or type the " +
        "four values into the <b>Advanced</b> tab by hand.</p>"
      );

      document.getElementById("copy").onclick = async (ev) => {
        const line =
          "oauth_token=" + data.token +
          ";client_id=" + data.clientId +
          ";username=" + data.username +
          ";user_id=" + data.userId;
        await navigator.clipboard.writeText(line);
        ev.target.textContent = "Copied \\u2014 now press \\u201cPaste login info\\u201d";
      };
    })
    .catch((e) => show('<p class="err">' + e + "</p>"));
}
</script>
</body></html>
`;

async function main() {
    const args = parseArgs(process.argv.slice(2));

    if (args.help || !args.clientId) {
        console.log(
            "usage: node tools/local-login/login.mjs --client-id <id> [--port 7777]\n\n" +
                "Register an application at https://dev.twitch.tv/console/apps with\n" +
                "OAuth Redirect URL http://localhost:7777 and Client Type 'Public'.\n" +
                "See tools/local-login/README.md.",
        );
        process.exit(args.help ? 0 : 1);
    }

    const scopes = readScopes();
    const redirectUri = `http://localhost:${args.port}`;

    const authorizeUrl =
        "https://id.twitch.tv/oauth2/authorize?" +
        new URLSearchParams({
            client_id: args.clientId,
            redirect_uri: redirectUri,
            response_type: "token",
            scope: scopes.join(" "),
            force_verify: "true",
        });

    const server = createServer(async (req, res) => {
        if (req.method === "POST" && req.url === "/token") {
            let body = "";
            for await (const chunk of req) {
                body += chunk;
            }

            let token;
            try {
                token = JSON.parse(body).token;
            } catch {
                res.writeHead(400, { "content-type": "application/json" });
                res.end(JSON.stringify({ error: "malformed request" }));
                return;
            }

            // Twitch's own validate endpoint gives the login and user id, and
            // proves the token works, in one call needing no extra scope.
            const response = await fetch("https://id.twitch.tv/oauth2/validate", {
                headers: { authorization: `OAuth ${token}` },
            });

            if (!response.ok) {
                res.writeHead(400, { "content-type": "application/json" });
                res.end(
                    JSON.stringify({
                        error: `Twitch rejected the token (${response.status}).`,
                    }),
                );
                return;
            }

            const info = await response.json();

            // The client id comes back from Twitch rather than from the
            // argument, so a token minted for some other application cannot be
            // paired with the wrong id and fail later for no visible reason.
            const result = {
                username: info.login,
                userId: info.user_id,
                clientId: info.client_id,
                token,
            };

            const missing = scopes.filter((s) => !(info.scopes ?? []).includes(s));

            console.log("\nSigned in as %s (id %s)", result.username, result.userId);
            console.log("  client id: %s", result.clientId);
            console.log("  token:     %s", mask(token));
            if (missing.length > 0) {
                console.log(
                    "\n  %d scope(s) were not granted: %s",
                    missing.length,
                    missing.join(", "),
                );
                console.log(
                    "  Features needing them will fail later rather than now.",
                );
            }
            console.log("\nCopy the values from the browser, then Ctrl+C here.");

            res.writeHead(200, { "content-type": "application/json" });
            res.end(JSON.stringify(result));
            return;
        }

        res.writeHead(200, { "content-type": "text/html; charset=utf-8" });
        res.end(page(redirectUri));
    });

    server.listen(args.port, "127.0.0.1", () => {
        console.log("Listening on %s", redirectUri);
        console.log("Asking for %d scope(s), read from %s", scopes.length,
                    "src/providers/twitch/TwitchAccountManager.cpp");
        console.log("\nOpen this and approve:\n\n%s\n", authorizeUrl);
    });
}

main().catch((error) => {
    console.error(String(error.message ?? error));
    process.exit(1);
});
