// ZeroTier network-ID short codes for the dArkOS netplay tool.
//   POST /publish {"nwid":"16hex"} -> {"code":"a1b2","expires":unix,"reused":bool}
//   GET  /resolve?code=a1b2        -> {"nwid":"16hex","expires":unix}
//   GET  /health                   -> {"ok":true,"rows":N,"active":M}
//
// One row per network (nwid UNIQUE): republishing a live code returns the same
// code and slides its 10-minute TTL; only an expired code is replaced. The
// cron purge keeps the table bounded by the number of active networks.
const TTL = 600;
const CODE_MIN = 4;   // 4 hex = 65k codes; set 3 only for tiny private use
const CODE_MAX = 6;
const TRIES_PER_LEN = 3;

const json = (obj, status = 200) =>
  new Response(JSON.stringify(obj), {
    status,
    headers: { "content-type": "application/json", "cache-control": "no-store" },
  });

const randHex = (len) => {
  const b = new Uint8Array(Math.ceil(len / 2));
  crypto.getRandomValues(b);
  return [...b].map((x) => x.toString(16).padStart(2, "0")).join("").slice(0, len);
};

// Best-effort per-isolate throttle; put Cloudflare Rate Limiting in front for
// real protection. Per-colo, resets with the isolate.
const hits = new Map();
const throttled = (ip) => {
  const now = Date.now();
  const e = hits.get(ip);
  if (!e || now > e.reset) {
    hits.set(ip, { n: 1, reset: now + 60_000 });
    return false;
  }
  return ++e.n > 10;
};

let schemaReady = false;
async function ensureSchema(env) {
  if (schemaReady) return;
  await env.DB.prepare(
    "CREATE TABLE IF NOT EXISTS codes (code TEXT PRIMARY KEY, nwid TEXT NOT NULL UNIQUE, expires INTEGER NOT NULL)"
  ).run();
  // Old deployments may have duplicate rows for one nwid; keep one before the
  // unique index is created. Only runs until the index exists, so cold starts
  // do not cause extra D1 writes.
  let haveIdx = false;
  try {
    const r = await env.DB.prepare(
      "SELECT name FROM sqlite_master WHERE type='index' AND name='idx_codes_nwid'"
    ).first();
    haveIdx = !!r;
  } catch {
    /* ignore */
  }
  if (!haveIdx) {
    try {
      await env.DB.prepare(
        "DELETE FROM codes WHERE rowid NOT IN (SELECT MAX(rowid) FROM codes GROUP BY nwid)"
      ).run();
      await env.DB.prepare(
        "CREATE UNIQUE INDEX IF NOT EXISTS idx_codes_nwid ON codes(nwid)"
      ).run();
    } catch {
      /* keep going: create-table already enforces UNIQUE on fresh databases */
    }
  }
  await env.DB.prepare(
    "CREATE INDEX IF NOT EXISTS idx_codes_expires ON codes(expires)"
  ).run();
  schemaReady = true;
}

const dbError = (e) => json({ error: "db", detail: String(e) }, 503);

export default {
  async fetch(request, env) {
    const url = new URL(request.url);

    if (request.method === "GET" && url.pathname === "/health") {
      try {
        await ensureSchema(env);
        const now = Math.floor(Date.now() / 1000);
        const total = await env.DB.prepare("SELECT COUNT(*) AS n FROM codes").first();
        const active = await env.DB.prepare(
          "SELECT COUNT(*) AS n FROM codes WHERE expires > ?"
        ).bind(now).first();
        return json({ ok: true, rows: total.n, active: active.n });
      } catch (e) {
        return json({ ok: false, error: String(e) }, 503);
      }
    }

    if (request.method === "POST" && url.pathname === "/publish") {
      const ip = request.headers.get("cf-connecting-ip") || "-";
      if (throttled(ip)) return json({ error: "rate limited" }, 429);
      let body;
      try {
        body = await request.json();
      } catch {
        return json({ error: "bad json" }, 400);
      }
      const nwid = String(body.nwid || "").toLowerCase();
      if (!/^[0-9a-f]{16}$/.test(nwid)) return json({ error: "bad nwid" }, 400);
      try {
        await ensureSchema(env);
      } catch (e) {
        return dbError(e);
      }
      const now = Math.floor(Date.now() / 1000);
      const expires = now + TTL;

      try {
        const row = await env.DB.prepare(
          "SELECT code,expires FROM codes WHERE nwid=?"
        ).bind(nwid).first();

        if (row && row.expires > now) {
          // Same network keeps its code; extend the validity window.
          await env.DB.prepare("UPDATE codes SET expires=? WHERE nwid=?")
            .bind(expires, nwid).run();
          return json({ code: row.code, expires, len: row.code.length, reused: true });
        }

        if (row) {
          // Expired: replace the code in place (still one row per network).
          for (let len = CODE_MIN; len <= CODE_MAX; len++) {
            for (let i = 0; i < TRIES_PER_LEN; i++) {
              const code = randHex(len);
              try {
                await env.DB.prepare("UPDATE codes SET code=?,expires=? WHERE nwid=?")
                  .bind(code, expires, nwid).run();
                return json({ code, expires, len });
              } catch (e) {
                if (!String(e).includes("UNIQUE")) return dbError(e);
              }
            }
          }
          return json({ error: "no code available" }, 500);
        }

        for (let len = CODE_MIN; len <= CODE_MAX; len++) {
          for (let i = 0; i < TRIES_PER_LEN; i++) {
            const code = randHex(len);
            try {
              await env.DB.prepare(
                "INSERT INTO codes(code,nwid,expires) VALUES(?,?,?)"
              ).bind(code, nwid, expires).run();
              return json({ code, expires, len });
            } catch (e) {
              const msg = String(e);
              if (msg.includes("UNIQUE") && msg.includes("nwid")) {
                // Concurrent publish won the race; return its code.
                const again = await env.DB.prepare(
                  "SELECT code,expires FROM codes WHERE nwid=?"
                ).bind(nwid).first();
                if (again) return json({ code: again.code, expires: again.expires,
                                        len: again.code.length, reused: true });
              }
              if (!msg.includes("UNIQUE")) return dbError(e);
            }
          }
        }
      } catch (e) {
        return dbError(e);
      }
      return json({ error: "no code available" }, 500);
    }

    if (request.method === "GET" && url.pathname === "/resolve") {
      const code = (url.searchParams.get("code") || "").toLowerCase();
      if (!/^[0-9a-f]{3,6}$/.test(code)) return json({ error: "bad code" }, 400);
      try {
        await ensureSchema(env);
        const row = await env.DB.prepare("SELECT nwid,expires FROM codes WHERE code=?")
          .bind(code).first();
        if (!row) return json({ error: "not found" }, 404);
        if (row.expires < Math.floor(Date.now() / 1000)) return json({ error: "expired" }, 404);
        return json({ nwid: row.nwid, expires: row.expires });
      } catch (e) {
        return dbError(e);
      }
    }

    return json({ error: "not found" }, 404);
  },

  async scheduled(_event, env) {
    await env.DB.prepare("DELETE FROM codes WHERE expires < ?")
      .bind(Math.floor(Date.now() / 1000))
      .run();
  },
};
