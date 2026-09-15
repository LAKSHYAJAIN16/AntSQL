'use strict';

// Per-identity token bucket. Each bucket refills continuously at
// `refillPerSec` tokens/second up to `capacity`, so a burst is allowed but
// sustained traffic is capped — simpler than a fixed window and doesn't
// reset all-or-nothing at a boundary.
class RateLimiter {
  constructor({ capacity, refillPerSec }) {
    this.capacity = capacity;
    this.refillPerSec = refillPerSec;
    this.buckets = new Map();
  }

  // Returns { allowed, retryAfterMs }. Consumes one token on success.
  take(identity) {
    const now = Date.now();
    let bucket = this.buckets.get(identity);
    if (!bucket) {
      bucket = { tokens: this.capacity, lastRefill: now };
      this.buckets.set(identity, bucket);
    }
    const elapsedSec = (now - bucket.lastRefill) / 1000;
    bucket.tokens = Math.min(this.capacity, bucket.tokens + elapsedSec * this.refillPerSec);
    bucket.lastRefill = now;

    if (bucket.tokens < 1) {
      const deficit = 1 - bucket.tokens;
      return { allowed: false, retryAfterMs: Math.ceil((deficit / this.refillPerSec) * 1000) };
    }
    bucket.tokens -= 1;
    return { allowed: true, retryAfterMs: 0 };
  }

  // Drops buckets that have been full and idle for a while, so long-running
  // processes don't accumulate one entry per API key/IP forever.
  sweep(idleMs = 10 * 60 * 1000) {
    const now = Date.now();
    for (const [identity, bucket] of this.buckets) {
      if (bucket.tokens >= this.capacity && now - bucket.lastRefill > idleMs) {
        this.buckets.delete(identity);
      }
    }
  }
}

module.exports = { RateLimiter };
