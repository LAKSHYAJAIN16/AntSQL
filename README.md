# AntSQL

AntSQL is a federated SQL gateway for edge and multi-site PostgreSQL shards.
The Python simulator in `sim/` is the research model; `engine/` is the C++23
production-routing core.

## Current C++ milestone

The core implements local pheromone route selection, cost-sensitive success
feedback, failure penalties, evaporation, exploration, and loop prevention.
An in-process forwarder and mutable in-memory topology provide a
dependency-free native multi-gateway harness; future gRPC forwarding and
discovery will implement the same interfaces. It can be tested with clang++
directly:

```powershell
clang++ -std=c++23 -I engine/include engine/src/router.cpp engine/src/gateway.cpp engine/src/in_process_forwarder.cpp engine/src/in_memory_topology.cpp engine/src/sql.cpp engine/tests/router_test.cpp engine/tests/gateway_test.cpp engine/tests/in_process_forwarder_test.cpp engine/tests/sql_test.cpp -o engine-router-tests.exe
.\engine-router-tests.exe
```

Arrow Flight SQL, gRPC forwarding, PostgreSQL/libpq execution, and the native
multi-node harness are the next layers built on this core.

## Deployment scaffold

[`deploy/`](deploy/) contains a Docker Compose stack for three PostgreSQL 16
shards and its initialization schema. It is ready to provision the database
cluster after Docker Desktop is installed, but it does not yet expose a
deployable AntSQL gateway: that requires the pending libpq, Arrow Flight SQL,
and gRPC adapters.
