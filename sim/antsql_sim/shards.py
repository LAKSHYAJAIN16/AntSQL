"""Shard placement: which node currently owns each shard.

Kept deliberately simple for v1 (§ related_work.md idea 9.1 — letting data
migrate toward demand via its own stigmergic process — is future work, not
this model). One primary owner node per shard; migration just reassigns
the owner. Routing strategies differ in how quickly they notice a migration
happened, which is the whole point.
"""
from __future__ import annotations

import numpy as np


class ShardMap:
    def __init__(self, n_shards: int, node_ids: list[int], seed: int):
        rng = np.random.default_rng(seed)
        self._owner: dict[int, int] = {
            shard_id: int(rng.choice(node_ids)) for shard_id in range(n_shards)
        }
        self.n_shards = n_shards

    def owner(self, shard_id: int) -> int:
        return self._owner[shard_id]

    def migrate(self, shard_id: int, new_owner: int) -> int:
        old = self._owner[shard_id]
        self._owner[shard_id] = new_owner
        return old
