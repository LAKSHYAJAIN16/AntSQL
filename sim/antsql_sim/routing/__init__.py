from antsql_sim.routing.adaptive_centralized import AdaptiveCentralized
from antsql_sim.routing.antsql import AntSQL
from antsql_sim.routing.decentralized_greedy import DecentralizedGreedy
from antsql_sim.routing.static_centralized import StaticCentralized

STRATEGIES = {
    "static_centralized": StaticCentralized,
    "adaptive_centralized": AdaptiveCentralized,
    "decentralized_greedy": DecentralizedGreedy,
    "antsql": AntSQL,
}
