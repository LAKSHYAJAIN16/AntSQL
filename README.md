# AntSQL

AntSQL is a federated SQL gateway for edge and multi-site PostgreSQL shards.
The Python simulator in `sim/` is the research model; `engine/` is the C++23
production-routing core.

## Current C++ milestone

The core implements local pheromone route selection, cost-sensitive success
feedback, failure penalties, evaporation, exploration, and loop prevention.
It has no external dependency yet and can be tested with clang++ directly:

```powershell
clang++ -std=c++23 -I engine/include engine/src/router.cpp engine/tests/router_test.cpp -o engine-router-tests.exe
.\engine-router-tests.exe
```

Arrow Flight SQL, gRPC forwarding, PostgreSQL/libpq execution, and the native
multi-node harness are the next layers built on this core.
