/*
 * spikeout-ticket (Cloudflare Worker) - entry tickets for the Supermodel PonMi streaming hosts.
 * 11/16 (owner): the client's Discord token goes only here, never to a host (requirement SV-16, design 3.8).
 *
 *   POST /ticket   {"token": "<Discord access token>", "host": "<host's public IP as in the lobby>",
 *                   "slot": 1..8, "nonce": <the host's one-time number>}
 *   200            {"ticket": "<base64 of the JSON payload>", "sig": "<base64 of the ECDSA P-256 signature (r|s)>"}
 *                  payload = {"v":1,"id":"<Discord user ID>","name":"<server nick / global name / user name>",
 *                             "host":"...","slot":n,"nonce":n,"exp":<unix seconds>}
 *   400 bad request / 401 login invalid / 403 not a member / 503 Discord could not be asked
 *   (503 {"error":"discord busy","retry_after":s} = Discord's rate limit: ask again after s seconds)
 *
 * The host checks the signature with the public key built into supermodel.exe, and that host, slot and nonce are its
 * own and exp has not passed. A ticket is good for one connection to one host only.
 *
 * Settings (Worker -> Settings -> Variables and Secrets):
 *   GUILD_ID     (Text)   the Discord server (guild) ID
 *   SIGNING_KEY  (Secret) the private key, ECDSA P-256, as JWK (made by Scripts/make_ticket_key.mjs)
 *
 * No secrets in this file; it may be published.
 */

const TICKET_LIFE_S = 60;
const NAME_MAX = 32;

let keyPromise = null;

function signingKey(env) {
  if (!keyPromise) {
    keyPromise = crypto.subtle.importKey('jwk', JSON.parse(env.SIGNING_KEY),
      { name: 'ECDSA', namedCurve: 'P-256' }, false, ['sign']);
  }
  return keyPromise;
}

function base64(bytes) {
  let s = '';
  for (const b of bytes) s += String.fromCharCode(b);
  return btoa(s);
}

// --- Membership look-up with a short memory (11/16 night) ---
// Discord rate-limits /users/@me/guilds/{guild}/member per login (HTTP 429 after a couple of calls in a few
// seconds), so joining, leaving and joining another slot right away was refused. A "member" answer is now kept for
// MEMBER_CACHE_S, keyed by the SHA-256 of the token (the token itself is not kept):
//  - in this isolate's memory (best effort: a Worker isolate lives for a while and serves nearby requests), and
//  - in the Cloudflare cache (caches.default; works on a custom domain, a harmless no-op on workers.dev).
// Every ticket is still signed fresh for one host, slot and nonce. Someone who left the Discord server can still
// get tickets for up to MEMBER_CACHE_S.
// A 429 with a short retry_after is waited out here once; a longer one is passed on as 503 with retry_after, and
// the client waits and asks again.
const MEMBER_CACHE_S = 600;
const MAX_WAIT_HERE_S = 3;
const memo = new Map();   // hash -> { id, name, until }

async function tokenHash(token) {
  const d = new Uint8Array(await crypto.subtle.digest('SHA-256', new TextEncoder().encode(token)));
  return Array.from(d, (b) => b.toString(16).padStart(2, '0')).join('');
}

function cacheUrl(hash) {
  return 'https://spikeout-ticket.cache/member/' + hash;
}

async function askDiscord(token, env) {
  return fetch(`https://discord.com/api/v10/users/@me/guilds/${env.GUILD_ID}/member`, {
    headers: { Authorization: 'Bearer ' + token },
  });
}

async function retryAfterOf(r) {
  let s = Number(r.headers.get('retry-after'));
  try {
    const j = await r.clone().json();
    if (typeof j.retry_after === 'number') s = j.retry_after;
  } catch {}
  return Number.isFinite(s) && s > 0 ? s : 1;
}

async function lookupMember(token, env) {
  const hash = await tokenHash(token);
  const now = Date.now();
  const hit = memo.get(hash);
  if (hit && hit.until > now) return { id: hit.id, name: hit.name };
  try {
    const c = await caches.default.match(cacheUrl(hash));
    if (c) {
      const v = await c.json();
      if (v && v.id) {
        memo.set(hash, { id: v.id, name: v.name || '', until: now + MEMBER_CACHE_S * 1000 });
        return { id: v.id, name: v.name || '' };
      }
    }
  } catch {}

  let r;
  try {
    r = await askDiscord(token, env);
    if (r.status === 429) {
      const wait = await retryAfterOf(r);
      if (wait <= MAX_WAIT_HERE_S) {
        await new Promise((res) => setTimeout(res, Math.ceil(wait * 1000) + 100));
        r = await askDiscord(token, env);
      }
    }
  } catch {
    return { error: true, status: 503, body: { error: 'discord unavailable' } };
  }
  if (r.status === 401) return { error: true, status: 401, body: { error: 'login invalid' } };
  if (r.status === 403 || r.status === 404) return { error: true, status: 403, body: { error: 'not member' } };
  if (r.status === 429)
    return { error: true, status: 503, body: { error: 'discord busy', status: 429, retry_after: await retryAfterOf(r) } };
  if (r.status !== 200) return { error: true, status: 503, body: { error: 'discord unavailable', status: r.status } };

  const member = await r.json();
  const user = member.user || {};
  if (!user.id) return { error: true, status: 503, body: { error: 'discord unavailable' } };
  const id = String(user.id);
  const name = String(member.nick || user.global_name || user.username || '').slice(0, NAME_MAX);

  if (memo.size > 2000) memo.clear();
  memo.set(hash, { id, name, until: now + MEMBER_CACHE_S * 1000 });
  try {
    await caches.default.put(cacheUrl(hash), new Response(JSON.stringify({ id, name }), {
      headers: { 'content-type': 'application/json', 'cache-control': 'max-age=' + MEMBER_CACHE_S },
    }));
  } catch {}
  return { id, name };
}

function json(obj, status = 200) {
  return new Response(JSON.stringify(obj), {
    status,
    headers: { 'content-type': 'application/json', 'cache-control': 'no-store' },
  });
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    if (url.pathname === '/' && request.method === 'GET')
      return json({ service: 'spikeout-ticket', ok: true });
    if (url.pathname !== '/ticket' || request.method !== 'POST')
      return json({ error: 'not found' }, 404);

    let body;
    try {
      body = await request.json();
    } catch {
      return json({ error: 'bad request' }, 400);
    }
    const token = typeof body.token === 'string' ? body.token : '';
    const host = typeof body.host === 'string' ? body.host : '';
    const slot = Number(body.slot);
    const nonce = Number(body.nonce);
    if (!token || token.length > 256 || !host || host.length > 64 ||
        !Number.isInteger(slot) || slot < 1 || slot > 8 ||
        !Number.isInteger(nonce) || nonce <= 0 || nonce > 0xFFFFFFFF)
      return json({ error: 'bad request' }, 400);

    // Is the token's owner a member of our Discord server? (needs the guilds.members.read scope)
    const m = await lookupMember(token, env);
    if (m.error) return json(m.body, m.status);
    const { id, name } = m;

    const payload = JSON.stringify({
      v: 1, id, name, host, slot, nonce,
      exp: Math.floor(Date.now() / 1000) + TICKET_LIFE_S,
    });
    const data = new TextEncoder().encode(payload);
    const sig = new Uint8Array(await crypto.subtle.sign(
      { name: 'ECDSA', hash: 'SHA-256' }, await signingKey(env), data));
    return json({ ticket: base64(data), sig: base64(sig) });
  },
};
