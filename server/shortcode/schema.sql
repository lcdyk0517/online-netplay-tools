CREATE TABLE IF NOT EXISTS codes (
  code    TEXT PRIMARY KEY,
  nwid    TEXT NOT NULL UNIQUE,
  expires INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_codes_expires ON codes(expires);
