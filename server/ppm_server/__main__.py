"""Run with python -m ppm_server init/serve/reset-password/backup."""

import argparse
from contextlib import closing
import getpass
import ipaddress
import logging
from pathlib import Path
import sqlite3
import ssl
from aiohttp import web
from .engine import NativeEngine
from .store import Store
from .service import create_app


def main():
    parser = argparse.ArgumentParser(description="PPM authenticated collaboration server")
    parser.add_argument(
        "--database",
        required=True,
        type=Path,
        help="Private local SQLite database (not a network share)",
    )
    sub = parser.add_subparsers(dest="command", required=True)
    init = sub.add_parser("init", help="Create the initial administrator interactively")
    init.add_argument("--user", required=True)
    reset = sub.add_parser(
        "reset-password", help="Operator password reset; revokes all user sessions"
    )
    reset.add_argument("--user", required=True)
    backup = sub.add_parser("backup", help="Consistent SQLite online backup")
    backup.add_argument("--output", required=True, type=Path)
    serve = sub.add_parser("serve")
    serve.add_argument("--engine", required=True, type=Path)
    serve.add_argument("--host", default="127.0.0.1")
    serve.add_argument("--port", type=int, default=8765)
    serve.add_argument("--certificate", type=Path)
    serve.add_argument("--private-key", type=Path)
    serve.add_argument("--public-origin", help="Exact HTTPS origin used by browser clients")
    args = parser.parse_args()
    store = Store(args.database)
    if args.command == "init":
        with store.connect() as db:
            if db.execute("SELECT 1 FROM users").fetchone():
                parser.error("Database already initialized")
        password = getpass.getpass("Administrator password (12+ characters): ")
        if password != getpass.getpass("Confirm password: "):
            parser.error("Passwords differ")
        store.add_user(args.user, password, True)
        print("Administrator created")
    elif args.command == "reset-password":
        password = getpass.getpass("New password: ")
        if password != getpass.getpass("Confirm password: "):
            parser.error("Passwords differ")
        store.reset_password(args.user, password)
        print("Password reset; previous sessions revoked")
    elif args.command == "backup":
        if args.output.exists():
            parser.error("Backup destination already exists")
        with store.connect() as source:
            with closing(sqlite3.connect(args.output)) as dest:
                source.backup(dest)
        print("Backup completed")
    else:
        try:
            loopback = ipaddress.ip_address(args.host).is_loopback
        except ValueError:
            loopback = args.host == "localhost"
        if bool(args.certificate) != bool(args.private_key):
            parser.error("Certificate and private key are paired")
        if not loopback and not args.certificate:
            parser.error("A non-loopback listener requires TLS")
        context = None
        if args.certificate:
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            context.minimum_version = ssl.TLSVersion.TLSv1_2
            context.load_cert_chain(args.certificate, args.private_key)
        if not loopback and not args.public_origin:
            parser.error("A remote listener requires --public-origin")
        origin = args.public_origin or (
            ("https" if context else "http") + "://" + args.host + ":" + str(args.port)
        )
        from urllib.parse import urlsplit

        url = urlsplit(origin)
        if (
            url.scheme not in ("https", "http")
            or not url.hostname
            or url.path
            or url.query
            or url.fragment
            or url.username
            or url.password
        ):
            parser.error("public-origin must be an exact origin without path or credentials")
        if context and url.scheme != "https":
            parser.error("TLS requires an HTTPS public origin")
        with store.connect() as db:
            if not db.execute("SELECT 1 FROM users WHERE admin=1 AND enabled=1").fetchone():
                parser.error("Initialize an administrator first")
        logging.basicConfig(level=logging.INFO)
        app = create_app(store, NativeEngine(args.engine), public_origin=origin)
        web.run_app(
            app,
            host=args.host,
            port=args.port,
            ssl_context=context,
            access_log=None,
            handler_args={"max_line_size": 8190, "max_field_size": 8190, "keepalive_timeout": 30},
        )


if __name__ == "__main__":
    main()
