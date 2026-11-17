"""Authoritative SQLite state. Project bytes, receipts and events commit together."""

from contextlib import contextmanager
import hashlib
import hmac
import json
import os
from pathlib import Path
import secrets
import sqlite3
import time


class ApiError(Exception):
    def __init__(self, status, code, message):
        super().__init__(message)
        self.status, self.code = status, code


def fail(status, code, message):
    raise ApiError(status, code, message)


def canonical(value):
    return json.dumps(
        value, ensure_ascii=False, sort_keys=True, separators=(",", ":"), allow_nan=False
    )


def digest(data):
    return hashlib.sha256(data).hexdigest()


def password_hash(password, salt, n=131072):
    return hashlib.scrypt(
        password.encode(), salt=salt, n=n, r=8, p=1, maxmem=256 * 1024 * 1024, dklen=32
    )


def valid_password(password):
    if not isinstance(password, str) or not 12 <= len(password) <= 256:
        fail(400, "password_policy", "Password must contain 12 to 256 characters")


def identifier(value):
    if (
        not isinstance(value, str)
        or not 1 <= len(value) <= 200
        or any(c in value for c in "\x00\r\n")
    ):
        fail(400, "invalid_id", "Invalid identifier")
    return value


class Store:
    def __init__(self, path, *, clock=time.time, scrypt_n=131072, session_ttl=28800, idle_ttl=1800):
        self.path = str(Path(path).resolve())
        self.clock, self.scrypt_n = clock, scrypt_n
        self.session_ttl, self.idle_ttl = session_ttl, idle_ttl
        Path(self.path).parent.mkdir(parents=True, exist_ok=True, mode=0o700)
        try:
            descriptor = os.open(self.path, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
        except FileExistsError:
            pass
        else:
            os.close(descriptor)
        with self.connect() as db:
            version = db.execute("PRAGMA user_version").fetchone()[0]
            if version not in (0, 1):
                raise RuntimeError("Unsupported server database version")
            db.execute("PRAGMA journal_mode=WAL")
            db.executescript("""
                CREATE TABLE IF NOT EXISTS users(id TEXT PRIMARY KEY, salt BLOB NOT NULL, verifier BLOB NOT NULL,
                    scrypt_n INTEGER NOT NULL, admin INTEGER NOT NULL DEFAULT 0, enabled INTEGER NOT NULL DEFAULT 1);
                CREATE TABLE IF NOT EXISTS sessions(hash TEXT PRIMARY KEY, user_id TEXT NOT NULL REFERENCES users(id),
                    expires REAL NOT NULL, touched REAL NOT NULL);
                CREATE TABLE IF NOT EXISTS attempts(key TEXT PRIMARY KEY, count INTEGER NOT NULL, until REAL NOT NULL);
                CREATE TABLE IF NOT EXISTS projects(id TEXT PRIMARY KEY, name TEXT NOT NULL, revision INTEGER NOT NULL,
                    version INTEGER NOT NULL, native BLOB NOT NULL, summary TEXT NOT NULL);
                CREATE TABLE IF NOT EXISTS grants(user_id TEXT NOT NULL REFERENCES users(id), project_id TEXT NOT NULL REFERENCES projects(id),
                    role TEXT NOT NULL, PRIMARY KEY(user_id, project_id));
                CREATE TABLE IF NOT EXISTS events(seq INTEGER PRIMARY KEY AUTOINCREMENT, project_id TEXT,
                    actor TEXT NOT NULL, kind TEXT NOT NULL, version INTEGER, detail TEXT NOT NULL, at REAL NOT NULL);
                CREATE TABLE IF NOT EXISTS previews(id TEXT PRIMARY KEY, user_id TEXT NOT NULL, project_id TEXT NOT NULL,
                    version INTEGER NOT NULL, request TEXT NOT NULL, hash TEXT NOT NULL, expires REAL NOT NULL, receipt TEXT NOT NULL);
                CREATE TABLE IF NOT EXISTS receipts(user_id TEXT NOT NULL, project_id TEXT NOT NULL, request_id TEXT NOT NULL,
                    hash TEXT NOT NULL, receipt TEXT NOT NULL, PRIMARY KEY(user_id, project_id, request_id));
                CREATE TABLE IF NOT EXISTS jobs(id TEXT PRIMARY KEY, owner TEXT NOT NULL REFERENCES users(id), project_id TEXT NOT NULL REFERENCES projects(id),
                    interval_seconds INTEGER NOT NULL, next_at REAL NOT NULL, expected_version INTEGER NOT NULL,
                    request TEXT NOT NULL, enabled INTEGER NOT NULL DEFAULT 1, state TEXT NOT NULL DEFAULT 'ready',
                    lease_until REAL, run_key TEXT, fail_count INTEGER NOT NULL DEFAULT 0);
                CREATE TABLE IF NOT EXISTS runs(id TEXT PRIMARY KEY, job_id TEXT NOT NULL REFERENCES jobs(id),
                    scheduled_at REAL NOT NULL, state TEXT NOT NULL, receipt TEXT, started REAL NOT NULL, finished REAL,
                    UNIQUE(job_id, scheduled_at));
                PRAGMA user_version=1;
            """)

    @contextmanager
    def connect(self, write=False):
        db = sqlite3.connect(self.path, timeout=5, isolation_level=None)
        db.row_factory = sqlite3.Row
        db.execute("PRAGMA foreign_keys=ON")
        db.execute("PRAGMA synchronous=FULL")
        try:
            if write:
                db.execute("BEGIN IMMEDIATE")
            yield db
            if write:
                db.commit()
        except BaseException:
            if write:
                db.rollback()
            raise
        finally:
            db.close()

    def add_user(self, user, password, admin=False, *, actor="bootstrap"):
        identifier(user)
        valid_password(password)
        salt = secrets.token_bytes(16)
        verifier = password_hash(password, salt, self.scrypt_n)
        with self.connect(True) as db:
            if db.execute("SELECT 1 FROM users WHERE id=?", (user,)).fetchone():
                fail(409, "user_exists", "User already exists")
            db.execute(
                "INSERT INTO users(id,salt,verifier,scrypt_n,admin) VALUES(?,?,?,?,?)",
                (user, salt, verifier, self.scrypt_n, int(admin)),
            )
            self.event(db, None, actor, "user-created", None, {"user": user, "admin": bool(admin)})

    def reset_password(self, user, password):
        valid_password(password)
        salt = secrets.token_bytes(16)
        verifier = password_hash(password, salt, self.scrypt_n)
        with self.connect(True) as db:
            if not db.execute("SELECT 1 FROM users WHERE id=?", (user,)).fetchone():
                fail(404, "not_found", "User not found")
            db.execute(
                "UPDATE users SET salt=?,verifier=?,scrypt_n=? WHERE id=?",
                (salt, verifier, self.scrypt_n, user),
            )
            db.execute("DELETE FROM sessions WHERE user_id=?", (user,))
            self.event(db, None, "administrator", "password-reset", None, {"user": user})

    def login(self, user, password, peer):
        identifier(user)
        if not isinstance(password, str) or len(password) > 256:
            fail(401, "invalid_credentials", "Invalid credentials")
        now = self.clock()
        key = digest((peer + "|" + user).encode())
        # Reserve an attempt before hashing so concurrent failures cannot bypass throttling.
        with self.connect(True) as db:
            db.execute("DELETE FROM attempts WHERE until<=?", (now,))
            attempt = db.execute("SELECT * FROM attempts WHERE key=?", (key,)).fetchone()
            peer_key = digest(("peer|" + peer).encode())
            peer_attempt = db.execute("SELECT * FROM attempts WHERE key=?", (peer_key,)).fetchone()
            if (attempt and attempt["count"] >= 5) or (
                peer_attempt and peer_attempt["count"] >= 30
            ):
                fail(429, "login_throttled", "Try again later")
            for k in (key, peer_key):
                db.execute(
                    "INSERT INTO attempts VALUES(?,1,?) ON CONFLICT(key) DO UPDATE SET count=count+1",
                    (k, now + 900),
                )
            row = db.execute("SELECT * FROM users WHERE id=?", (user,)).fetchone()
        salt = row["salt"] if row else bytes(16)
        verifier = password_hash(password, salt, row["scrypt_n"] if row else self.scrypt_n)
        if not row or not hmac.compare_digest(verifier, row["verifier"]) or not row["enabled"]:
            fail(401, "invalid_credentials", "Invalid credentials")
        token = secrets.token_urlsafe(32)
        with self.connect(True) as db:
            # Recheck account state after the expensive KDF.
            current = db.execute("SELECT * FROM users WHERE id=?", (user,)).fetchone()
            if not current["enabled"] or current["verifier"] != row["verifier"]:
                fail(401, "invalid_credentials", "Invalid credentials")
            db.execute("DELETE FROM attempts WHERE key=?", (key,))
            db.execute("UPDATE attempts SET count=max(0,count-1) WHERE key=?", (peer_key,))
            db.execute("DELETE FROM attempts WHERE key=? AND count=0", (peer_key,))
            db.execute(
                "DELETE FROM sessions WHERE expires<=? OR touched<=?", (now, now - self.idle_ttl)
            )
            if (
                db.execute("SELECT count(*) FROM sessions WHERE user_id=?", (user,)).fetchone()[0]
                >= 20
            ):
                fail(429, "session_limit", "Revoke an existing session before logging in")
            db.execute(
                "INSERT INTO sessions VALUES(?,?,?,?)",
                (digest(token.encode()), user, now + self.session_ttl, now),
            )
            self.event(db, None, user, "login", None, {})
        return {
            "token": token,
            "user": user,
            "admin": bool(row["admin"]),
            "expiresAt": now + self.session_ttl,
        }

    def authenticate(self, token, *, touch=True):
        if not isinstance(token, str) or not 20 <= len(token) <= 200:
            fail(401, "authentication_required", "Authentication required")
        now = self.clock()
        with self.connect(touch) as db:
            row = db.execute(
                "SELECT u.id,u.admin,u.enabled,s.expires,s.touched FROM sessions s JOIN users u ON u.id=s.user_id WHERE s.hash=?",
                (digest(token.encode()),),
            ).fetchone()
            if (
                not row
                or not row["enabled"]
                or row["expires"] <= now
                or row["touched"] + self.idle_ttl <= now
            ):
                fail(401, "session_expired", "Session expired or revoked")
            if touch:
                db.execute(
                    "UPDATE sessions SET touched=? WHERE hash=?", (now, digest(token.encode()))
                )
            return {"id": row["id"], "admin": bool(row["admin"])}

    def logout(self, token):
        with self.connect(True) as db:
            db.execute("DELETE FROM sessions WHERE hash=?", (digest(token.encode()),))

    def authorize(self, db, user, project_id, action="view"):
        current = db.execute("SELECT enabled,admin FROM users WHERE id=?", (user["id"],)).fetchone()
        if not current or not current["enabled"]:
            fail(401, "account_disabled", "Account disabled")
        if current["admin"]:
            return
        row = db.execute(
            "SELECT role FROM grants WHERE user_id=? AND project_id=?", (user["id"], project_id)
        ).fetchone()
        allowed = row and (
            action == "view"
            or (action == "edit" and row["role"] == "editor")
            or (action == "approve" and row["role"] == "reviewer")
        )
        if not allowed:
            fail(403, "forbidden", "Project permission denied")

    def require_admin(self, db, user):
        row = db.execute("SELECT enabled,admin FROM users WHERE id=?", (user["id"],)).fetchone()
        if not row or not row["enabled"] or not row["admin"]:
            fail(403, "administrator_required", "Administrator required")

    def event(self, db, project, actor, kind, version, detail):
        cursor = db.execute(
            "INSERT INTO events(project_id,actor,kind,version,detail,at) VALUES(?,?,?,?,?,?)",
            (project, actor, kind, version, canonical(detail), self.clock()),
        )
        return cursor.lastrowid

    def project(self, user, project_id):
        with self.connect() as db:
            self.authorize(db, user, project_id)
            row = db.execute("SELECT * FROM projects WHERE id=?", (project_id,)).fetchone()
            if not row:
                fail(404, "not_found", "Project not found")
            return dict(row)

    def projects(self, user):
        with self.connect() as db:
            current = db.execute(
                "SELECT admin,enabled FROM users WHERE id=?", (user["id"],)
            ).fetchone()
            if not current or not current["enabled"]:
                fail(401, "account_disabled", "Account disabled")
            rows = db.execute(
                "SELECT id,name,revision,version FROM projects WHERE ? OR id IN (SELECT project_id FROM grants WHERE user_id=?) ORDER BY name,id",
                (current["admin"], user["id"]),
            ).fetchall()
            return [dict(r) for r in rows]

    def changes(self, user, after, limit=100):
        with self.connect() as db:
            current = db.execute(
                "SELECT admin,enabled FROM users WHERE id=?", (user["id"],)
            ).fetchone()
            if not current or not current["enabled"]:
                fail(401, "account_disabled", "Account disabled")
            rows = db.execute(
                "SELECT * FROM events WHERE seq>? ORDER BY seq LIMIT ?", (after, limit)
            ).fetchall()
            events = []
            for r in rows:
                if r["project_id"] is None:
                    allowed = bool(current["admin"])
                else:
                    allowed = bool(current["admin"]) or bool(
                        db.execute(
                            "SELECT 1 FROM grants WHERE user_id=? AND project_id=?",
                            (user["id"], r["project_id"]),
                        ).fetchone()
                    )
                if (
                    r["kind"] == "permission-changed"
                    and json.loads(r["detail"]).get("user") == user["id"]
                ):
                    allowed = True
                if allowed:
                    events.append(
                        {
                            "seq": r["seq"],
                            "projectId": r["project_id"],
                            "actor": r["actor"],
                            "kind": r["kind"],
                            "version": r["version"],
                            "detail": json.loads(r["detail"]),
                            "at": r["at"],
                        }
                    )
            return {
                "cursor": rows[-1]["seq"] if rows else after,
                "events": events,
                "more": len(rows) == limit,
            }
