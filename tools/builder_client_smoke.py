#!/usr/bin/env python3

import argparse
import http.client
import json
import os
import re
import secrets
import socket
import subprocess
import sys
import tempfile
import time
import uuid
from argparse import Namespace
from pathlib import Path

import account_smoke


BUILDER_GAME_PORT = 4802
DEFAULT_BUILDER_PASSWORD = account_smoke.DEFAULT_CREATE_PASSWORD
LOOPBACK_HOST = account_smoke.LOOPBACK_HOST


def redact_text(value: str) -> str:
    redacted = re.sub(r"Bearer\s+[A-Za-z0-9._~+/=-]+", "Bearer [redacted]", value)
    redacted = re.sub(
        r"(?i)(token|password)([=:]\s*)[^&\s,;\"']+", r"\1\2[redacted]", redacted
    )
    return re.sub(r"(?i)([?&](?:access_)?token=)[^&\s]+", r"\1[redacted]", redacted)


def redact_response(value):
    if isinstance(value, dict):
        redacted = {}
        for key, nested in value.items():
            lowered = key.lower()
            redacted[key] = (
                "<redacted>"
                if "token" in lowered or "password" in lowered
                else redact_response(nested)
            )
        return redacted
    if isinstance(value, list):
        return [redact_response(item) for item in value]
    if isinstance(value, str):
        return redact_text(value)
    return value


def fnv1a64(text: str) -> str:
    value = 1469598103934665603
    for byte in text.encode("utf-8"):
        value ^= byte
        value = (value * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return f"fnv1a64:{value:016x}"


def append_int(parts: list[str], name: str, value: int) -> None:
    parts.append(f"{name}={value}\n")


def append_string(parts: list[str], name: str, value: str) -> None:
    parts.append(f"{name}={len(value)}:{value}\n")


def package_checksum(package: dict) -> str:
    parts: list[str] = []
    append_int(parts, "format", package["packageFormatVersion"])
    append_int(parts, "vnum", package["vnum"])
    append_int(parts, "host", {"character": 0, "object": 1, "room": 2, "mudlle-mobile": 3}[package["host"]])
    append_int(parts, "manifest_schema", package["manifestSchemaVersion"])
    append_int(parts, "trigger_catalog", package["triggerCatalogRevision"])
    append_string(parts, "manifest_checksum", package["manifestChecksum"])
    append_string(parts, "runtime_name", package["runtimeName"])
    append_string(parts, "runtime_version", package["runtimeVersion"])
    append_string(parts, "typings", package["generatedTypingsVersion"])
    append_string(parts, "compiled_js", package["compiledJavaScript"])
    for binding in package["triggerBindings"]:
        append_int(parts, "binding_kind", {"legacy-script-trigger": 0, "mudlle-call-flag": 1}[binding["kind"]])
        append_int(parts, "binding_value", binding["legacyValue"])
        append_string(parts, "binding_handler", binding["handlerName"])
    return fnv1a64("".join(parts))


def assert_persisted_activation(
    live_store_path: Path,
    package_id: str,
    staged_digest: str,
    live_checksum: str,
) -> None:
    if not live_store_path.exists():
        raise RuntimeError(f"Smoke live store was not persisted at {live_store_path}")
    live_store = json.loads(live_store_path.read_text(encoding="utf-8"))
    pointers = live_store.get("live_pointers", [])
    for pointer in pointers:
        if (
            pointer.get("package_id") == package_id
            and pointer.get("staged_digest") == staged_digest
            and pointer.get("current_live_checksum") == live_checksum
            and live_checksum != "live:old"
        ):
            return
    raise RuntimeError(
        "Smoke live store did not contain the activated package pointer: "
        f"packageId={package_id} stagedDigest={staged_digest} liveChecksum={live_checksum}"
    )


def assert_no_persisted_activation(live_store_path: Path, package_id: str) -> None:
    if not live_store_path.exists():
        return
    live_store = json.loads(live_store_path.read_text(encoding="utf-8"))
    for pointer in live_store.get("live_pointers", []):
        if pointer.get("package_id") == package_id:
            raise RuntimeError(
                "Rejected activation unexpectedly wrote a live pointer: "
                f"packageId={package_id} pointer={redact_response(pointer)}"
            )


def http_json(port: int, method: str, path: str, body: dict | None = None, token: str | None = None) -> tuple[int, dict]:
    encoded = b"" if body is None else json.dumps(body, separators=(",", ":")).encode("utf-8")
    headers = {"content-type": "application/json"}
    if token:
        headers["authorization"] = f"Bearer {token}"
    conn = http.client.HTTPConnection(LOOPBACK_HOST, port, timeout=10)
    try:
        conn.request(method, path, body=encoded, headers=headers)
        response = conn.getresponse()
        raw = response.read()
        parsed = json.loads(raw.decode("utf-8")) if raw else {}
        return response.status, parsed
    finally:
        conn.close()


def wait_for_builder_api(port: int, timeout_seconds: float) -> None:
    deadline = time.time() + timeout_seconds
    last_error: Exception | None = None
    while time.time() < deadline:
        try:
            status, _ = http_json(port, "GET", "/api/builder/js/manifest")
            if status in (200, 403, 503):
                return
        except Exception as error:
            last_error = error
        time.sleep(0.1)
    raise RuntimeError(f"Timed out waiting for Builder API on {port}: {last_error}")


def create_account_with_character(args: Namespace, repo_root: Path, account_email: str, character_name: str, temp_dir: Path) -> Path:
    capture_path = temp_dir / "verification-email.txt"
    capture_script_path = temp_dir / "capture-sendmail.sh"
    capture_script_path.write_text("#!/bin/sh\n" f"cat > '{capture_path}'\n" "exit 0\n", encoding="utf-8")
    capture_script_path.chmod(0o700)

    game_log_path = temp_dir / "account-game.log"
    proxy_log_path = temp_dir / "account-proxy.log"
    game_binary = repo_root / "bin" / "ageland"
    game_env = account_smoke.smoke_child_env({"ROTS_SENDMAIL_COMMAND": str(capture_script_path)})
    game_log = game_log_path.open("wb")
    proxy_log = proxy_log_path.open("wb")
    game_process = None
    proxy_process = None
    try:
        game_process = subprocess.Popen(
            [str(game_binary), "-x", str(args.account_game_port)],
            cwd=repo_root,
            env=game_env,
            stdout=game_log,
            stderr=subprocess.STDOUT,
        )
        account_smoke.wait_for_process_port(
            game_process,
            "account setup game server",
            LOOPBACK_HOST,
            args.account_game_port,
            args.startup_timeout,
            game_log_path,
        )

        proxy_process = subprocess.Popen(
            [
                "cargo",
                "run",
                "-p",
                "proxy",
                "--",
                "--game",
                f"127.0.0.1:{args.account_game_port}",
                "--listen",
                f"127.0.0.1:{args.account_proxy_port}",
                "--websocket",
                f"127.0.0.1:{args.account_websocket_port}",
                "--builder-api",
                f"127.0.0.1:{args.unused_builder_api_port}",
            ],
            cwd=repo_root,
            env=account_smoke.smoke_child_env({"RUST_LOG": "info"}),
            stdout=proxy_log,
            stderr=subprocess.STDOUT,
        )
        account_smoke.wait_for_process_log_marker(
            proxy_process,
            "account setup proxy",
            proxy_log_path,
            f"Listening for TCP connections on {LOOPBACK_HOST}:{args.account_proxy_port}",
            args.startup_timeout,
        )

        with socket.create_connection((LOOPBACK_HOST, args.account_proxy_port), timeout=5) as sock:
            reader = account_smoke.BufferedPromptReader(sock)
            account_smoke.wait_for_initial_account_email_prompt(reader, 8.0)
            account_smoke.send_line(sock, account_email)
            reader.recv_until(["Create one? (Y/N):"], 8.0)
            account_smoke.send_line(sock, "y")
            reader.recv_until(["Please enter a password:"], 8.0)
            account_smoke.send_line(sock, DEFAULT_BUILDER_PASSWORD)
            reader.recv_until(["Please retype your password:"], 8.0)
            account_smoke.send_line(sock, DEFAULT_BUILDER_PASSWORD)
            reader.recv_until([account_smoke.VERIFICATION_CODE_PROMPT], 8.0)
            account_smoke.complete_email_verification(sock, reader, capture_path, args.verification_timeout)
            account_smoke.send_line(sock, "4")
            reader.recv_until(["New character name:"], 8.0)
            account_smoke.send_line(sock, character_name)
            reader.recv_until(["suitable name for roleplay in Middle-earth"], 8.0)
            account_smoke.send_line(sock, "y")
            reader.recv_until(["What is your sex (M/F)?"], 8.0)
            account_smoke.send_line(sock, "m")
            reader.recv_until(["Race:"], 8.0)
            account_smoke.send_line(sock, "h")
            reader.recv_until(["Class:"], 8.0)
            account_smoke.send_line(sock, "a")
            reader.recv_until(["Do you wish to enable the default colour set (Y/N)?"], 8.0)
            account_smoke.send_line(sock, "n")
            reader.recv_until(["Do you see an 'a' with a pair of dots above it:"], 8.0)
            account_smoke.send_line(sock, "n")
            reader.recv_until(["Make your choice:"], 8.0)
            account_smoke.send_line(sock, "0")

        account_file = account_smoke.find_account_file_for_email(repo_root, account_email)
        if account_file is None:
            raise RuntimeError(f"Builder smoke account file was not created for {account_email}.")
        return account_file
    finally:
        account_smoke.terminate_process(proxy_process)
        account_smoke.terminate_process(game_process)
        proxy_log.close()
        game_log.close()


def promote_account_character(account_file: Path, character_name: str) -> int:
    character_path = account_smoke.account_native_character_file(account_file, character_name)
    data = json.loads(character_path.read_text(encoding="utf-8"))
    data["progression"]["level"] = 95
    character_path.write_text(json.dumps(data, indent=2, sort_keys=False) + "\n", encoding="utf-8")
    return int(data["identity"]["idnum"])


def runtime_parts(runtime_identity: str) -> tuple[str, str]:
    if "-" not in runtime_identity:
        return runtime_identity, ""
    name, version = runtime_identity.split("-", 1)
    return name, version


def first_character_trigger(manifest: dict) -> dict:
    trigger_manifest = manifest.get("triggerManifest")
    triggers = trigger_manifest.get("triggers", []) if isinstance(trigger_manifest, dict) else manifest.get("triggers", [])
    for trigger in triggers:
        if trigger.get("supportStatus") in ("active", "deferred") and "character" in trigger.get("hostTypes", []):
            return trigger
    raise RuntimeError("Manifest did not contain an active character trigger.")


def build_package(manifest: dict, vnum: int, package_id: str) -> dict:
    compatibility = manifest.get("compatibility") if isinstance(manifest.get("compatibility"), dict) else manifest
    runtime_name = compatibility.get("runtimeName")
    runtime_version = compatibility.get("runtimeVersion")
    if not runtime_name:
        runtime_name, runtime_version = runtime_parts(str(compatibility["runtimeIdentity"]))
    trigger = first_character_trigger(manifest)
    package = {
        "packageFormatVersion": int(compatibility["packageFormatVersion"]),
        "packageId": package_id,
        "host": "character",
        "vnum": vnum,
        "manifestSchemaVersion": int(manifest["schemaVersion"]),
        "triggerCatalogRevision": int(compatibility["triggerCatalogRevision"]),
        "manifestChecksum": compatibility["triggerManifestChecksum"],
        "runtimeName": runtime_name,
        "runtimeVersion": runtime_version,
        "generatedTypingsVersion": compatibility["generatedTypingsVersion"],
        "compiledJavaScript": "exports.%s = function(ctx) { return true; };" % trigger["handlerName"],
        "compiledJavaScriptChecksum": "",
        "triggerBindings": [
            {
                "kind": trigger["kind"],
                "legacyValue": int(trigger["legacyValue"]),
                "handlerName": trigger["handlerName"],
            }
        ],
    }
    package["compiledJavaScriptChecksum"] = package_checksum(package)
    return package


def run_builder_smoke_attempt(args: Namespace, repo_root: Path) -> int:
    if args.builder_game_port != BUILDER_GAME_PORT:
        raise RuntimeError("BuilderClient smoke must use game port 4802 to match proxy target policy.")
    game_binary = repo_root / "bin" / "ageland"
    if not game_binary.exists():
        raise RuntimeError(f"Missing game binary at {game_binary}. Build the server before running smoke.")

    smoke_id = f"bld{uuid.uuid4().hex[:12]}"
    account_email = f"{smoke_id}@example.com"
    character_name = account_smoke.make_unused_character_name(repo_root)
    proxy_secret = f"builder-smoke-{secrets.token_urlsafe(24)}"
    temp_dir = Path(tempfile.mkdtemp(prefix="rots-builder-smoke-"))
    builder_game_log_path = temp_dir / "builder-game.log"
    builder_proxy_log_path = temp_dir / "builder-proxy.log"
    live_store_relative_path = f"builder-smoke-live-stores/{smoke_id}/js_live_store.json"
    publish_audit_relative_path = f"builder-smoke-live-stores/{smoke_id}/js_publish_audit.jsonl"
    live_store_path = repo_root / "lib" / live_store_relative_path
    publish_audit_path = repo_root / "lib" / publish_audit_relative_path
    account_file: Path | None = None
    game_process = None
    proxy_process = None
    builder_game_log = None
    builder_proxy_log = None
    passed = False

    try:
        live_store_path.parent.mkdir(parents=True, exist_ok=True)
        account_file = create_account_with_character(
            args, repo_root, account_email, character_name, temp_dir
        )
        promote_account_character(account_file, character_name)

        builder_game_log = builder_game_log_path.open("wb")
        game_process = subprocess.Popen(
            [str(game_binary), "-b", "-p", str(args.builder_game_port)],
            cwd=repo_root,
            env=account_smoke.smoke_child_env(
                {
                    "ROTS_BUILDER_PROXY_SECRET": proxy_secret,
                    "ROTS_JS_LIVE_STORE_PATH": live_store_relative_path,
                    "ROTS_JS_PUBLISH_AUDIT_PATH": publish_audit_relative_path,
                }
            ),
            stdout=builder_game_log,
            stderr=subprocess.STDOUT,
        )
        account_smoke.wait_for_process_port(
            game_process,
            "builder game server",
            LOOPBACK_HOST,
            args.builder_game_port,
            args.startup_timeout,
            builder_game_log_path,
        )

        builder_proxy_log = builder_proxy_log_path.open("wb")
        proxy_process = subprocess.Popen(
            [
                "cargo",
                "run",
                "-p",
                "proxy",
                "--",
                "--game",
                f"127.0.0.1:{args.unused_game_port}",
                "--builder-game",
                f"127.0.0.1:{args.builder_game_port}",
                "--listen",
                f"127.0.0.1:{args.unused_proxy_port}",
                "--websocket",
                f"127.0.0.1:{args.unused_websocket_port}",
                "--builder-api",
                f"127.0.0.1:{args.builder_api_port}",
            ],
            cwd=repo_root,
            env=account_smoke.smoke_child_env(
                {"RUST_LOG": "info", "ROTS_BUILDER_PROXY_SECRET": proxy_secret}
            ),
            stdout=builder_proxy_log,
            stderr=subprocess.STDOUT,
        )
        account_smoke.wait_for_process_log_marker(
            proxy_process,
            "builder proxy",
            builder_proxy_log_path,
            f"Listening for BuilderClient API connections on {LOOPBACK_HOST}:{args.builder_api_port}",
            args.startup_timeout,
        )
        wait_for_builder_api(args.builder_api_port, args.startup_timeout)

        status, login = http_json(
            args.builder_api_port,
            "POST",
            "/api/builder/login",
            {"account": account_email, "password": DEFAULT_BUILDER_PASSWORD, "requestId": smoke_id},
        )
        if status != 200 or not login.get("ok"):
            raise RuntimeError(f"Builder login failed: HTTP {status} {redact_response(login)}")
        token = login.get("token")
        if not token:
            raise RuntimeError(f"Builder login did not return a token: {redact_response(login)}")
        if character_name[:1].upper() + character_name[1:].lower() not in login.get("immortalCharacterNames", []):
            raise RuntimeError(
                f"Builder login did not report promoted immortal: {redact_response(login)}"
            )

        status, manifest = http_json(args.builder_api_port, "GET", "/api/builder/js/manifest")
        if status != 200 or manifest.get("exportKind") != "builderManifest":
            raise RuntimeError(f"Manifest request failed: HTTP {status} {redact_response(manifest)}")

        stage_package = build_package(manifest, 4802, f"builder-smoke-{smoke_id}")
        status, staged = http_json(
            args.builder_api_port,
            "POST",
            "/api/builder/js/stage",
            {"baseLiveChecksum": "live:old", "package": stage_package},
            token,
        )
        if status != 200 or not staged.get("ok") or not staged.get("stagedDigest"):
            raise RuntimeError(f"Stage failed: HTTP {status} {redact_response(staged)}")
        staged_package_id = staged.get("packageId")
        staged_digest = staged.get("stagedDigest")
        if not staged_package_id:
            raise RuntimeError(f"Stage response did not include package id: {staged}")

        status, staged_status = http_json(
            args.builder_api_port,
            "POST",
            "/api/builder/js/status",
            {"packageId": staged_package_id},
            token,
        )
        if (
            status != 200
            or not staged_status.get("ok")
            or staged_status.get("reasonCode") != "status.current"
            or not staged_status.get("stagedDigest")
        ):
            raise RuntimeError(
                f"Status after stage failed: HTTP {status} {redact_response(staged_status)}; "
                f"staged={redact_response(staged)}"
            )
        staged_digest = staged_status.get("stagedDigest")

        status, stale_live_activation = http_json(
            args.builder_api_port,
            "POST",
            "/api/builder/js/activate",
            {
                "packageId": staged_package_id,
                "stagedDigest": staged_digest,
                "baseLiveChecksum": "live:stale-smoke",
            },
            token,
        )
        if (
            status != 409
            or stale_live_activation.get("ok") is not False
            or stale_live_activation.get("reasonCode") != "activate.stale-live-checksum"
            or stale_live_activation.get("packageId") != staged_package_id
            or stale_live_activation.get("stagedDigest") != staged_digest
            or stale_live_activation.get("liveChecksum") != "live:old"
        ):
            raise RuntimeError(
                f"Stale-live activation unexpectedly changed state: HTTP {status} "
                f"{redact_response(stale_live_activation)}"
            )
        status, conflict_status = http_json(
            args.builder_api_port,
            "POST",
            "/api/builder/js/status",
            {"packageId": staged_package_id},
            token,
        )
        if (
            status != 200
            or not conflict_status.get("ok")
            or conflict_status.get("reasonCode") != "status.current"
            or conflict_status.get("packageId") != staged_package_id
            or conflict_status.get("stagedDigest") != staged_digest
            or conflict_status.get("liveChecksum") != "live:old"
        ):
            raise RuntimeError(
                f"Status after stale-live activation failed: HTTP {status} "
                f"{redact_response(conflict_status)}"
            )
        assert_no_persisted_activation(live_store_path, staged_package_id)

        status, stale_digest_activation = http_json(
            args.builder_api_port,
            "POST",
            "/api/builder/js/activate",
            {
                "packageId": staged_package_id,
                "stagedDigest": "sha256:not-the-reviewed-smoke-digest",
                "baseLiveChecksum": "live:old",
            },
            token,
        )
        if (
            status != 403
            or stale_digest_activation.get("ok") is not False
            or stale_digest_activation.get("reasonCode") != "activate.authorization-failed"
            or stale_digest_activation.get("packageId")
            or stale_digest_activation.get("stagedDigest")
            or stale_digest_activation.get("liveChecksum")
        ):
            raise RuntimeError(
                f"Stale-digest activation leaked metadata or changed state: HTTP {status} "
                f"{redact_response(stale_digest_activation)}"
            )
        status, stale_digest_status = http_json(
            args.builder_api_port,
            "POST",
            "/api/builder/js/status",
            {"packageId": staged_package_id},
            token,
        )
        if (
            status != 200
            or not stale_digest_status.get("ok")
            or stale_digest_status.get("reasonCode") != "status.current"
            or stale_digest_status.get("packageId") != staged_package_id
            or stale_digest_status.get("stagedDigest") != staged_digest
            or stale_digest_status.get("liveChecksum") != "live:old"
        ):
            raise RuntimeError(
                f"Status after stale-digest activation failed: HTTP {status} "
                f"{redact_response(stale_digest_status)}"
            )
        assert_no_persisted_activation(live_store_path, staged_package_id)

        status, activated = http_json(
            args.builder_api_port,
            "POST",
            "/api/builder/js/activate",
            {
                "packageId": staged_package_id,
                "stagedDigest": staged_digest,
                "baseLiveChecksum": "live:old",
            },
            token,
        )
        if status != 200 or not activated.get("ok") or activated.get("reasonCode") != "activate.accepted":
            raise RuntimeError(
                f"Activate failed: HTTP {status} {redact_response(activated)}; "
                f"stagedStatus={redact_response(staged_status)}"
            )
        status, activated_status = http_json(
            args.builder_api_port,
            "POST",
            "/api/builder/js/status",
            {"packageId": staged_package_id},
            token,
        )
        if (
            status != 200
            or not activated_status.get("ok")
            or activated_status.get("reasonCode") != "status.current"
            or activated_status.get("packageId") != staged_package_id
            or activated_status.get("stagedDigest") != staged_digest
            or activated_status.get("liveChecksum") in ("", "live:old", None)
            or (
                activated.get("liveChecksum")
                and activated_status.get("liveChecksum") != activated.get("liveChecksum")
            )
        ):
            raise RuntimeError(
                f"Status after activate failed: HTTP {status} "
                f"{redact_response(activated_status)}"
            )
        assert_persisted_activation(
            live_store_path,
            staged_package_id,
            staged_digest,
            activated_status.get("liveChecksum"),
        )
        activated_live_checksum = activated_status.get("liveChecksum")

        status, replay_activation = http_json(
            args.builder_api_port,
            "POST",
            "/api/builder/js/activate",
            {
                "packageId": staged_package_id,
                "stagedDigest": staged_digest,
                "baseLiveChecksum": "live:old",
            },
            token,
        )
        if (
            status != 409
            or replay_activation.get("ok") is not False
            or replay_activation.get("reasonCode") != "activate.stale-live-checksum"
            or replay_activation.get("packageId") != staged_package_id
            or replay_activation.get("stagedDigest") != staged_digest
            or replay_activation.get("liveChecksum") != activated_live_checksum
        ):
            raise RuntimeError(
                f"Activation replay did not preserve current live state: HTTP {status} "
                f"{redact_response(replay_activation)}"
            )
        status, replay_status = http_json(
            args.builder_api_port,
            "POST",
            "/api/builder/js/status",
            {"packageId": staged_package_id},
            token,
        )
        if (
            status != 200
            or not replay_status.get("ok")
            or replay_status.get("reasonCode") != "status.current"
            or replay_status.get("packageId") != staged_package_id
            or replay_status.get("stagedDigest") != staged_digest
            or replay_status.get("liveChecksum") != activated_live_checksum
        ):
            raise RuntimeError(
                f"Status after activation replay failed: HTTP {status} "
                f"{redact_response(replay_status)}"
            )
        assert_persisted_activation(
            live_store_path,
            staged_package_id,
            staged_digest,
            activated_live_checksum,
        )

        wrong_zone_package = build_package(manifest, 999999, f"builder-smoke-wrong-zone-{smoke_id}")
        status, wrong_zone = http_json(
            args.builder_api_port,
            "POST",
            "/api/builder/js/stage",
            {"baseLiveChecksum": "live:old", "package": wrong_zone_package},
            token,
        )
        if status < 400 or wrong_zone.get("ok") is not False:
            raise RuntimeError(
                f"Wrong-zone upload unexpectedly succeeded: HTTP {status} "
                f"{redact_response(wrong_zone)}"
            )

        status, logout = http_json(args.builder_api_port, "POST", "/api/builder/logout", None, token)
        if status != 200 or not logout.get("ok"):
            raise RuntimeError(f"Builder logout failed: HTTP {status} {redact_response(logout)}")
        status, post_logout_status = http_json(
            args.builder_api_port,
            "POST",
            "/api/builder/js/status",
            {"packageId": staged_package_id},
            token,
        )
        if status < 400 or post_logout_status.get("ok") is not False:
            raise RuntimeError(
                f"Logged-out token unexpectedly remained usable: HTTP {status} "
                f"{redact_response(post_logout_status)}"
            )
        status, post_logout_rollback = http_json(
            args.builder_api_port,
            "POST",
            "/api/builder/js/rollback",
            {
                "packageId": staged_package_id,
                "targetLiveChecksum": activated_live_checksum,
                "reason": "smoke revoked-token rollback",
            },
            token,
        )
        if (
            status < 400
            or post_logout_rollback.get("ok") is not False
            or post_logout_rollback.get("packageId")
            or post_logout_rollback.get("stagedDigest")
            or post_logout_rollback.get("liveChecksum")
        ):
            raise RuntimeError(
                f"Logged-out rollback unexpectedly exposed metadata or succeeded: HTTP {status} "
                f"{redact_response(post_logout_rollback)}"
            )

        offline_command = subprocess.run(
            ["npm", "test", "--", "src/core/compiler.test.ts", "src/core/offlineRunner.test.ts"],
            cwd=repo_root / "BuilderClient",
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        if offline_command.returncode != 0:
            raise RuntimeError(f"Offline BuilderClient tests failed:\n{offline_command.stdout}")

        passed = True
        print(
            "BuilderClient smoke passed: temporary account -> promoted immortal -> login -> manifest -> stage -> stale-live conflict -> stale-digest conflict -> activate -> replay conflict -> wrong-zone rejection -> logout denials -> offline unauthenticated tests."
        )
        return 0
    finally:
        account_smoke.terminate_process(proxy_process)
        account_smoke.terminate_process(game_process)
        if builder_proxy_log is not None:
            builder_proxy_log.close()
        if builder_game_log is not None:
            builder_game_log.close()

        if not args.keep_artifacts and passed and account_file is not None:
            account_smoke.cleanup_smoke_account_character_directory(
                repo_root, json.loads(account_file.read_text(encoding="utf-8")).get("account_name"), character_name
            )
            account_smoke.cleanup_account_native_character_files(account_file, character_name)
            account_smoke.remove_if_exists(account_file)
            account_smoke.remove_empty_directory(account_file.parent)

        if not args.keep_artifacts and passed:
            account_smoke.remove_if_exists(live_store_path)
            account_smoke.remove_if_exists(publish_audit_path)
            account_smoke.remove_empty_directory(live_store_path.parent)
            account_smoke.remove_empty_directory(live_store_path.parent.parent)
            if live_store_path.exists():
                raise RuntimeError(f"Smoke live store cleanup failed for {live_store_path}")
            if publish_audit_path.exists():
                raise RuntimeError(f"Smoke publish audit cleanup failed for {publish_audit_path}")
            account_smoke.remove_if_exists(temp_dir)
        else:
            print(f"Kept smoke artifacts in {temp_dir}")
            if live_store_path.exists():
                print(f"Kept smoke live store at {live_store_path}")
            if publish_audit_path.exists():
                print(f"Kept smoke publish audit at {publish_audit_path}")
            if account_file is not None:
                print(f"Kept smoke account file at {account_file}")
                character_path = account_smoke.account_native_character_file(account_file, character_name)
                if character_path.exists():
                    print(f"Kept smoke character file at {character_path}")
            if builder_game_log_path.exists():
                print(f"Kept builder game log at {builder_game_log_path}")
            if builder_proxy_log_path.exists():
                print(f"Kept builder proxy log at {builder_proxy_log_path}")


def resolve_ports(args: Namespace) -> None:
    reserved = {args.builder_game_port}
    for name in (
        "account_game_port",
        "account_proxy_port",
        "account_websocket_port",
        "unused_builder_api_port",
        "builder_api_port",
        "unused_game_port",
        "unused_proxy_port",
        "unused_websocket_port",
    ):
        value = getattr(args, name)
        if value == 0:
            value = account_smoke.allocate_free_tcp_port(maximum=65535, reserved_ports=reserved)
            setattr(args, name, value)
        if value in reserved:
            raise RuntimeError(f"Smoke-test TCP ports must be distinct; {value} was reused.")
        reserved.add(value)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Smoke test BuilderClient account auth and JavaScript publish through the Rust proxy."
    )
    parser.add_argument("--repo-root", default=str(Path(__file__).resolve().parent.parent))
    parser.add_argument("--builder-game-port", type=int, default=BUILDER_GAME_PORT)
    parser.add_argument("--builder-api-port", type=int, default=0)
    parser.add_argument("--account-game-port", type=int, default=0)
    parser.add_argument("--account-proxy-port", type=int, default=0)
    parser.add_argument("--account-websocket-port", type=int, default=0)
    parser.add_argument("--unused-builder-api-port", type=int, default=0)
    parser.add_argument("--unused-game-port", type=int, default=0)
    parser.add_argument("--unused-proxy-port", type=int, default=0)
    parser.add_argument("--unused-websocket-port", type=int, default=0)
    parser.add_argument("--startup-timeout", type=float, default=30.0)
    parser.add_argument("--verification-timeout", type=float, default=15.0)
    parser.add_argument("--keep-artifacts", action="store_true")
    args = parser.parse_args()
    resolve_ports(args)
    return run_builder_smoke_attempt(args, Path(args.repo_root).resolve())


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(
            "Successful smoke artifacts are cleaned up by default; failed attempts print preserved /tmp and repo-local fixture paths for debugging.",
            file=sys.stderr,
        )
        print(f"BuilderClient smoke failed: {error}", file=sys.stderr)
        sys.exit(1)
