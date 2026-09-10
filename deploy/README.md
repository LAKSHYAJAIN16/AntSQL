# Local PostgreSQL shard cluster

This Compose stack starts three independent PostgreSQL 16 shards for AntSQL
development. They are durable database nodes only: the current C++ engine does
not yet include its libpq executor, Arrow Flight SQL endpoint, or gRPC
forwarder, so this stack does **not** expose an AntSQL gateway service.

## Start

Install Docker Desktop with Compose v2, then from the repository root:

```powershell
Copy-Item deploy/.env.example deploy/.env
# Edit deploy/.env and set a non-default password.
docker compose --env-file deploy/.env -f deploy/docker-compose.yml up -d
docker compose --env-file deploy/.env -f deploy/docker-compose.yml ps
```

The default host connections are:

| Shard | Host connection |
| --- | --- |
| `shard-0` | `postgresql://antsql:<password>@localhost:5433/antsql` |
| `shard-1` | `postgresql://antsql:<password>@localhost:5434/antsql` |
| `shard-2` | `postgresql://antsql:<password>@localhost:5435/antsql` |

Each shard initializes the `readings(site_id, recorded_at, value)` table. Load
only the keys assigned to each shard; the Compose stack deliberately leaves
ownership policy to AntSQL's topology layer.

## Verify and stop

```powershell
docker compose --env-file deploy/.env -f deploy/docker-compose.yml exec shard-0 psql -U antsql -d antsql -c "SELECT 1"
docker compose --env-file deploy/.env -f deploy/docker-compose.yml down
```

`down` preserves shard volumes. To remove all local database data as well, use
`down --volumes` only when that data is disposable.
