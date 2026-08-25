import networkx as nx

from antsql_sim.routing.antsql import AntSQL
from antsql_sim.shards import ShardMap


def graph():
    g = nx.Graph()
    g.add_edges_from([(0, 1), (1, 2), (0, 3), (3, 2)])
    for u, v in g.edges:
        g.edges[u, v]["latency"] = 1.0
        g.edges[u, v]["alive"] = True
    for node in g.nodes:
        g.nodes[node]["alive"] = True
    return g


def strategy(**kwargs):
    g = graph()
    shards = ShardMap(n_shards=2, node_ids=list(g.nodes), seed=4)
    return AntSQL(g, shards, list(g.nodes), seed=5, **kwargs)


def test_query_routing_never_revisits_a_node():
    ant = strategy(exploit_probability=1.0)
    # Make node 1 look strongly attractive, then exclude it as visited.
    ant.pheromone[0] = {0: {1: 100.0, 3: 1.0}}
    assert ant.next_hop(0, 0, visited={0, 1}) == 3


def test_successful_query_reinforces_its_path():
    ant = strategy()
    before = ant.pheromone[0].get(0, {}).get(1, ant._init_pher)
    ant.report_result([0, 1, 2], target_shard=0, success=True, cost=2.0)
    assert ant.pheromone[0][0][1] > before


def test_failed_query_does_not_reinforce_its_path():
    ant = strategy()
    ant.report_result([0, 1], target_shard=0, success=False, cost=1.0)
    assert 0 not in ant.pheromone[0]


def test_gossip_respects_per_node_shard_budget():
    ant = strategy(hop_count=1, gossip_shards_per_node=1, gossip_r=1.0)
    ant.pheromone[0] = {0: {1: 9.0}, 1: {3: 1.0}}
    ant._gossip_round(ant.graph)
    # Node 1 is adjacent to 0. Only the strongest shard advertisement (0)
    # may be introduced by node 0's gossip budget.
    assert 0 in ant.pheromone[1]
    assert 1 not in ant.pheromone[1]
