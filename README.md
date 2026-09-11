# AntSQL

AntSQL is a federated SQL gateway for edge and multi-site PostgreSQL shards.
The Python simulator in `sim/` is the research model; `engine/` is the C++23
production-routing core.

## Current C++ milestone

The core implements local pheromone route selection, cost-sensitive success
feedback, failure penalties, evaporation, exploration, and loop prevention.
An in-process forwarder and mutable in-memory topology provide a
dependency-free native multi-gateway harness for single-process tests. A
second `IForwarder` implementation, `TcpForwarder`/`TcpServer`, forwards the
same routing contract over real TCP sockets (Winsock on Windows) with a
hand-rolled, bounds-checked wire format — so multi-process/multi-machine
routing and failure behavior can be exercised today without a gRPC or Arrow
Flight SQL toolchain. A future gRPC/Flight SQL forwarder implements the same
`IForwarder` interface and can replace either without changing `Gateway` or
the routing contract. It can be tested with clang++ directly:

```powershell
clang++ -std=c++23 -I engine/include engine/src/router.cpp engine/src/gateway.cpp engine/src/in_process_forwarder.cpp engine/src/in_memory_topology.cpp engine/src/sql.cpp engine/src/wire.cpp engine/src/tcp_forwarder.cpp engine/tests/router_test.cpp engine/tests/gateway_test.cpp engine/tests/in_process_forwarder_test.cpp engine/tests/sql_test.cpp engine/tests/wire_test.cpp engine/tests/tcp_forwarder_test.cpp -lws2_32 -o engine-router-tests.exe
.\engine-router-tests.exe
```

Arrow Flight SQL, gRPC forwarding, PostgreSQL/libpq execution, and connection
pooling for the TCP transport are the next layers built on this core.

## Deployment scaffold

[`deploy/`](deploy/) contains a Docker Compose stack for three PostgreSQL 16
shards and its initialization schema. It is ready to provision the database
cluster after Docker Desktop is installed, but it does not yet expose a
deployable AntSQL gateway: that requires the pending libpq, Arrow Flight SQL,
and gRPC adapters.
