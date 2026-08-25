"""Network topology generation.

Nodes are simulation sites; edges carry a base latency (ms). Topologies are
generated once per trial from a seed, then mutated at runtime by churn
events (node death, latency shift) — see churn.py.
"""
from __future__ import annotations

import networkx as nx
import numpy as np


def generate_topology(kind: str, n: int, seed: int, avg_degree: float = 3.5) -> nx.Graph:
    """Return a connected graph with a 'latency' float attribute (ms) on every edge.

    kind:
      'nsfnet_like' — irregular, sparsely-connected WAN-style graph (mirrors
        the AntNet testbed's degree of connectivity at whatever n is given).
      'random'      — Erdos-Renyi-ish random graph at the given avg_degree,
        for topology-size sweeps.
    """
    rng = np.random.default_rng(seed)
    if kind == "nsfnet_like":
        g = nx.random_regular_graph(d=min(3, n - 1), n=n, seed=seed)
    elif kind == "random":
        p = min(1.0, avg_degree / max(1, n - 1))
        g = nx.gnp_random_graph(n=n, p=p, seed=seed)
    else:
        raise ValueError(f"unknown topology kind: {kind}")

    # Ensure connectivity: stitch any disconnected components together.
    if not nx.is_connected(g):
        components = list(nx.connected_components(g))
        for i in range(len(components) - 1):
            u = next(iter(components[i]))
            v = next(iter(components[i + 1]))
            g.add_edge(u, v)

    for u, v in g.edges():
        g.edges[u, v]["latency"] = float(rng.uniform(1.0, 20.0))  # ms, ~AntNet's NSFNET range
        g.edges[u, v]["alive"] = True

    for node in g.nodes():
        g.nodes[node]["alive"] = True

    return g


def alive_subgraph(g: nx.Graph) -> nx.Graph:
    """View of g containing only alive nodes and alive edges between them."""
    alive_nodes = [n for n in g.nodes() if g.nodes[n]["alive"]]
    h = g.subgraph(alive_nodes).copy()
    dead_edges = [(u, v) for u, v in h.edges() if not h.edges[u, v]["alive"]]
    h.remove_edges_from(dead_edges)
    return h


def is_alive(g: nx.Graph, node: int) -> bool:
    """Constant-time node liveness check for routing hot paths."""
    return node in g and bool(g.nodes[node].get("alive", False))


def alive_neighbors(g: nx.Graph, node: int) -> list[int]:
    """Return live adjacent nodes without constructing an induced graph."""
    if not is_alive(g, node):
        return []
    return [
        neighbor
        for neighbor in g.neighbors(node)
        if is_alive(g, neighbor) and g.edges[node, neighbor].get("alive", True)
    ]
