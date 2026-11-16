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
    let r;
    try {
      r = await fetch(`https://discord.com/api/v10/users/@me/guilds/${env.GUILD_ID}/member`, {
        headers: { Authorization: 'Bearer ' + token },
      });
    } catch {
      return json({ error: 'discord unavailable' }, 503);
    }
    if (r.status === 401) return json({ error: 'login invalid' }, 401);
    if (r.status === 403 || r.status === 404) return json({ error: 'not member' }, 403);
    if (r.status !== 200) return json({ error: 'discord unavailable', status: r.status }, 503);

    const member = await r.json();
    const user = member.user || {};
    if (!user.id) return json({ error: 'discord unavailable' }, 503);
    const name = String(member.nick || user.global_name || user.username || '').slice(0, NAME_MAX);

    const payload = JSON.stringify({
      v: 1, id: String(user.id), name, host, slot, nonce,
      exp: Math.floor(Date.now() / 1000) + TICKET_LIFE_S,
    });
    const data = new TextEncoder().encode(payload);
    const sig = new Uint8Array(await crypto.subtle.sign(
      { name: 'ECDSA', hash: 'SHA-256' }, await signingKey(env), data));
    return json({ ticket: base64(data), sig: base64(sig) });
  },
};
