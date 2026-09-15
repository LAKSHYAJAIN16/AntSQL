'use strict';

// A JS port of engine/include/antsql/router.hpp's pheromone-routing
// algorithm (same fields, same formulas), applied here to replica
// selection instead of gateway-to-gateway hops. Kept as a faithful port
// rather than a from-scratch reimplementation so the product's behavior
// stays traceable to the research engine it's built on.
class Router {
  constructor(config = {}) {
    this.config = {
      evaporationRate: 0.02,
      baseReinforcement: 2.5,
      referenceCostMs: 8.0,
      minimumReinforcementFraction: 0.02,
      failurePenalty: 0.25,
      explorationProbability: 0.06,
      initialPheromone: 0.5,
      ...config,
    };
    // routeKey (string) -> Map(candidateId -> pheromone score)
    this.pheromone = new Map();
  }

  _routeMap(key) {
    let m = this.pheromone.get(key);
    if (!m) {
      m = new Map();
      this.pheromone.set(key, m);
    }
    return m;
  }

  pheromoneFor(key, candidateId) {
    const m = this.pheromone.get(key);
    if (!m || !m.has(candidateId)) return this.config.initialPheromone;
    return m.get(candidateId);
  }

  _score(key, candidate) {
    return this.pheromoneFor(key, candidate.id) / Math.max(candidate.linkLatencyMs, 0.001);
  }

  // candidates: [{id, linkLatencyMs}]; visited: Set<id> to exclude.
  choose(key, candidates, visited = new Set()) {
    const eligible = candidates.filter((c) => !visited.has(c.id));
    if (eligible.length === 0) return null;

    if (Math.random() < this.config.explorationProbability) {
      const chosen = eligible[Math.floor(Math.random() * eligible.length)];
      return { nextHop: chosen.id, score: this._score(key, chosen), explored: true };
    }

    let best = eligible[0];
    let bestScore = this._score(key, best);
    for (const candidate of eligible.slice(1)) {
      const score = this._score(key, candidate);
      if (score > bestScore) {
        best = candidate;
        bestScore = score;
      }
    }
    return { nextHop: best.id, score: bestScore, explored: false };
  }

  observeSuccess(key, candidateId, totalCostMs) {
    const quality = this.config.referenceCostMs / Math.max(totalCostMs, 0.001);
    const reinforcement =
      this.config.baseReinforcement *
      Math.min(1.0, Math.max(this.config.minimumReinforcementFraction, quality));
    const m = this._routeMap(key);
    m.set(candidateId, this.pheromoneFor(key, candidateId) + reinforcement);
  }

  observeFailure(key, candidateId) {
    const m = this._routeMap(key);
    m.set(candidateId, Math.max(0, this.pheromoneFor(key, candidateId) * this.config.failurePenalty));
  }

  evaporate() {
    for (const m of this.pheromone.values()) {
      for (const [candidateId, score] of m) m.set(candidateId, score * (1 - this.config.evaporationRate));
    }
  }
}

module.exports = { Router };
