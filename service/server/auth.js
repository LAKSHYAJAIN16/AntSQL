'use strict';

const fs = require('fs');
const crypto = require('crypto');

// One API key == one isolated tenant namespace. No accounts, no email,
// no passwords: creating a key IS signing up, the same "hit an endpoint,
// get a working credential" flow Firebase/Fauna's quickstarts lead with.
// Each key carries metadata (createdAt, revoked) rather than being a bare
// string in a set, so revocation can be tracked without deleting history.
class KeyStore {
  constructor(filePath) {
    this.filePath = filePath;
    this.keys = new Map();
    if (fs.existsSync(filePath)) {
      const parsed = JSON.parse(fs.readFileSync(filePath, 'utf8'));
      // Back-compat: older files stored a flat array of key strings.
      if (Array.isArray(parsed)) {
        for (const key of parsed) this.keys.set(key, { createdAt: null, revoked: false });
      } else {
        for (const [key, meta] of Object.entries(parsed)) this.keys.set(key, meta);
      }
    }
  }

  _save() {
    fs.writeFileSync(this.filePath, JSON.stringify(Object.fromEntries(this.keys), null, 2));
  }

  create() {
    const key = `ant_${crypto.randomBytes(24).toString('hex')}`;
    this.keys.set(key, { createdAt: new Date().toISOString(), revoked: false });
    this._save();
    return key;
  }

  isValid(key) {
    const meta = typeof key === 'string' ? this.keys.get(key) : undefined;
    return !!meta && !meta.revoked;
  }

  // Revokes a key so it can no longer authenticate. A key can only revoke
  // itself (there are no admin accounts in this model) — the caller must
  // already have proven possession of the key via normal auth.
  revoke(key) {
    const meta = this.keys.get(key);
    if (!meta || meta.revoked) return false;
    meta.revoked = true;
    meta.revokedAt = new Date().toISOString();
    this._save();
    return true;
  }
}

module.exports = { KeyStore };
