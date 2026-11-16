// make_ticket_key.mjs - makes the signing key pair of the entry-ticket Worker (Server/ticket-worker, design 3.8).
//
//   node Scripts/make_ticket_key.mjs
//
// - Config/TicketSigningKey.private.jwk : the PRIVATE key (JWK). Paste its content into the Worker's secret
//   SIGNING_KEY, then delete the file. It is in .gitignore; never commit or send it.
// - prints the PUBLIC key (65-byte uncompressed P-256 point, base64) for Config/StreamingKeys.ini (TicketPublicKey).
//   The public key may be published.
// Refuses to overwrite an existing private key file (a new key needs a new build of supermodel.exe).

import { webcrypto } from 'node:crypto';
import { existsSync, writeFileSync, mkdirSync } from 'node:fs';

const out = 'Config/TicketSigningKey.private.jwk';
if (existsSync(out)) {
  console.error(out + ' already exists - delete it first if you really want a new key.');
  process.exit(1);
}
const pair = await webcrypto.subtle.generateKey({ name: 'ECDSA', namedCurve: 'P-256' }, true, ['sign', 'verify']);
const priv = await webcrypto.subtle.exportKey('jwk', pair.privateKey);
const pub = new Uint8Array(await webcrypto.subtle.exportKey('raw', pair.publicKey));
mkdirSync('Config', { recursive: true });
writeFileSync(out, JSON.stringify(priv), { mode: 0o600 });
console.log('private key written to ' + out + ' (paste into the Worker secret SIGNING_KEY, then delete it)');
console.log('TicketPublicKey = ' + Buffer.from(pub).toString('base64'));
