/**
 * Shared test doubles.
 *
 * Both the hub and the HTTP routes need an identity source and a moderator
 * lookup that answer without touching the network.
 */

import { AuthError, type Identity, type Validator } from "../src/auth.ts";
import type { ModerationLookup } from "../src/modules/moderation.ts";

export class FakeValidator implements Validator {
  private readonly known = new Map<string, Identity>();

  add(token: string, twitchId: string, login: string): this {
    this.known.set(token, {
      twitchId,
      login,
      displayName: login,
      scopes: [],
    });
    return this;
  }

  validate(token: string): Promise<Identity> {
    const stripped = token.replace(/^(OAuth|Bearer)\s+/i, "").trim();
    const identity = this.known.get(stripped);
    if (identity === undefined) {
      return Promise.reject(new AuthError("token rejected by Twitch", 401));
    }
    return Promise.resolve(identity);
  }
}

export class FakeModeration implements ModerationLookup {
  private readonly grants = new Map<string, Set<string>>();

  grant(userId: string, channelId: string): this {
    let channels = this.grants.get(userId);
    if (channels === undefined) {
      channels = new Set();
      this.grants.set(userId, channels);
    }
    channels.add(channelId);
    return this;
  }

  moderates(_token: string, userId: string, channelId: string): Promise<boolean> {
    if (userId === channelId) {
      return Promise.resolve(true);
    }
    return Promise.resolve(this.grants.get(userId)?.has(channelId) ?? false);
  }

  moderatedChannels(_token: string, userId: string): Promise<Set<string>> {
    return Promise.resolve(new Set(this.grants.get(userId) ?? []));
  }
}
