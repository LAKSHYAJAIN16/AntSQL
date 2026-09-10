-- Every physical shard receives the same schema. AntSQL routes a query to a
-- single shard using its exact site_id distribution key; this script does not
-- use PostgreSQL's own table partitioning.
CREATE TABLE IF NOT EXISTS readings (
  site_id INTEGER NOT NULL,
  recorded_at TIMESTAMPTZ NOT NULL,
  value DOUBLE PRECISION NOT NULL,
  PRIMARY KEY (site_id, recorded_at)
);

CREATE INDEX IF NOT EXISTS readings_recorded_at_idx ON readings (recorded_at);
