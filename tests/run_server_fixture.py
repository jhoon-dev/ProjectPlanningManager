"""Private loopback service used only by the native integration/browser checks."""

import argparse
import asyncio
import json
from pathlib import Path
import sys
import ssl
import socket
from aiohttp import web

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "server"))
from ppm_server.engine import NativeEngine
from ppm_server.service import create_app
from ppm_server.store import Store, canonical


async def run(args):
    store = Store(args.database, scrypt_n=16384)
    password = "PPM integration password 123"
    for name, admin in [("admin", True), ("editor", False), ("viewer", False)]:
        store.add_user(name, password, admin)
    engine = NativeEngine(args.engine)
    native = (ROOT / "tests/fixtures/server-project.ppm").read_bytes()
    summary, _, _ = await engine.evaluate(native)
    with store.connect(True) as db:
        db.execute(
            "INSERT INTO projects VALUES(?,?,?,1,?,?)",
            (summary["documentId"], "Shared demo", summary["revision"], native, canonical(summary)),
        )
        db.execute("INSERT INTO grants VALUES('editor',?,'editor')", (summary["documentId"],))
        db.execute("INSERT INTO grants VALUES('viewer',?,'viewer')", (summary["documentId"],))
        store.event(db, summary["documentId"], "admin", "project-created", 1, {})
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.bind(("127.0.0.1", args.port))
    port = listener.getsockname()[1]
    url = f"{'https' if args.tls else 'http'}://127.0.0.1:{port}"
    app = create_app(store, engine, scheduler=args.scheduler, public_origin=url)
    runner = web.AppRunner(app, access_log=None)
    await runner.setup()
    context = None
    if args.tls:
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(
            ROOT / "tests/fixtures/server-tls/certificate.pem",
            ROOT / "tests/fixtures/server-tls/test-only-key.pem",
        )
    site = web.SockSite(runner, listener, ssl_context=context)
    await site.start()
    # Native consumers use existence as the readiness signal. Publish only
    # after the complete JSON is closed, so they cannot read an empty file.
    ready = Path(args.ready)
    pending = ready.with_name(ready.name + ".pending")
    pending.write_text(
        json.dumps({"url": url, "projectId": summary["documentId"]}), encoding="utf-8"
    )
    pending.replace(ready)
    try:
        await asyncio.Event().wait()
    finally:
        await runner.cleanup()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--database", required=True)
    parser.add_argument("--engine", required=True)
    parser.add_argument("--ready", required=True)
    parser.add_argument("--port", type=int, default=0)
    parser.add_argument("--scheduler", action="store_true")
    parser.add_argument("--tls", action="store_true", help="Use the public test-only certificate")
    asyncio.run(run(parser.parse_args()))
