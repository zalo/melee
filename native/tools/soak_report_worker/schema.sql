-- One row per soak report. The header columns repeat the report's own `key: value` lines so the
-- index can be filtered without unpacking anything; `body` is the report as sent (gzip).
CREATE TABLE IF NOT EXISTS reports (
  id TEXT PRIMARY KEY,
  received TEXT NOT NULL,
  client TEXT NOT NULL,
  size INTEGER NOT NULL,
  result TEXT,
  date TEXT,
  version TEXT,
  binary TEXT,
  modes TEXT,
  device TEXT,
  cfw TEXT,
  kernel TEXT,
  memtotal TEXT,
  matches INTEGER,
  last TEXT,
  body BLOB NOT NULL
);
CREATE INDEX IF NOT EXISTS reports_received ON reports (received);
