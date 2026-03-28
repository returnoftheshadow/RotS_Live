#!/usr/bin/env python3

import argparse
import json
import os
import re
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time
import uuid
from pathlib import Path


DEFAULT_CREATE_PASSWORD = "ValidPass1"
DEFAULT_RESET_PASSWORD = "BetterPass2"
DEFAULT_CHARACTER_PASSWORD = "HeroPw1"


def account_bucket_for_name(name: str) -> str:
    normalized = name.strip().lower()
    if not normalized:
        return "ZZZ"

    first = normalized[0]
    if first in "abcde":
        return "A-E"
    if first in "fghij":
        return "F-J"
    if first in "klmno":
        return "K-O"
    if first in "pqrst":
        return "P-T"
    if first in "uvwxyz":
        return "U-Z"
    return "ZZZ"


def wait_for_port(host: str, port: int, timeout_seconds: float) -> None:
    deadline = time.time() + timeout_seconds
    while time.time() < deadline:
        try:
            with socket.create_connection((host, port), timeout=0.5):
                return
        except OSError:
            time.sleep(0.1)

    raise RuntimeError(f"Timed out waiting for {host}:{port} to accept connections.")


def recv_until(sock: socket.socket, markers: list[str], timeout_seconds: float) -> str:
    deadline = time.time() + timeout_seconds
    data = b""
    sock.settimeout(0.5)

    while time.time() < deadline:
        try:
            chunk = sock.recv(4096)
        except socket.timeout:
            continue

        if not chunk:
            break

        data += chunk
        text = data.decode("latin1", errors="ignore")
        if any(marker in text for marker in markers):
            return text

    text = data.decode("latin1", errors="ignore")
    raise RuntimeError(
        "Timed out waiting for markers "
        + ", ".join(markers)
        + ". Last output was:\n"
        + text[-800:]
    )


def send_line(sock: socket.socket, line: str) -> None:
    sock.sendall(line.encode("utf-8") + b"\n")


def wait_for_verification_code(capture_path: Path, timeout_seconds: float) -> str:
    deadline = time.time() + timeout_seconds
    pattern = re.compile(r"Verification code:\s*(\d{6})")

    while time.time() < deadline:
        if capture_path.exists():
            contents = capture_path.read_text(encoding="utf-8", errors="ignore")
            match = pattern.search(contents)
            if match:
                return match.group(1)
        time.sleep(0.1)

    raise RuntimeError(f"Timed out waiting for a verification code in {capture_path}.")


def terminate_process(process: subprocess.Popen[bytes] | None) -> None:
    if process is None or process.poll() is not None:
        return

    process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)


def remove_if_exists(path: Path) -> None:
    if path.exists():
        if path.is_dir():
            shutil.rmtree(path)
        else:
            path.unlink()


def find_account_file_for_email(repo_root: Path, account_email: str) -> Path | None:
    accounts_root = repo_root / "lib" / "accounts"
    if not accounts_root.exists():
        return None

    for candidate in accounts_root.rglob("*.json"):
        try:
            account_data = json.loads(candidate.read_text(encoding="utf-8"))
        except Exception:
            continue

        if account_data.get("normalized_email") == account_email:
            return candidate

    return None


def read_json_file(path: Path) -> dict | None:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except Exception:
        return None


def character_bucket_for_name(name: str) -> str:
    return account_bucket_for_name(name)


def legacy_player_file_path(repo_root: Path, character_name: str) -> Path:
    normalized = character_name.strip().lower()
    return repo_root / "lib" / "players" / character_bucket_for_name(normalized) / normalized


def cleanup_legacy_player_files(repo_root: Path, character_name: str) -> None:
    canonical_path = legacy_player_file_path(repo_root, character_name)
    remove_if_exists(canonical_path)
    for candidate in canonical_path.parent.glob(f"{canonical_path.name}.*"):
        remove_if_exists(candidate)


def legacy_object_file_path(repo_root: Path, character_name: str) -> Path:
    normalized = character_name.strip().lower()
    return repo_root / "lib" / "plrobjs" / character_bucket_for_name(normalized) / f"{normalized}.obj"


def legacy_exploits_file_path(repo_root: Path, character_name: str) -> Path:
    normalized = character_name.strip().lower()
    return repo_root / "lib" / "exploits" / character_bucket_for_name(normalized) / f"{normalized}.exploits"


def account_character_directory(repo_root: Path, account_name: str, character_name: str) -> Path:
    normalized_account = account_name.strip().lower()
    normalized_character = character_name.strip().lower()
    return (
        repo_root
        / "lib"
        / "account_characters"
        / account_bucket_for_name(normalized_account)
        / normalized_account
        / normalized_character
    )


def cleanup_character_artifacts(repo_root: Path, account_name: str | None, character_name: str) -> None:
    cleanup_legacy_player_files(repo_root, character_name)
    remove_if_exists(legacy_object_file_path(repo_root, character_name))
    remove_if_exists(legacy_exploits_file_path(repo_root, character_name))
    if account_name:
        remove_if_exists(account_character_directory(repo_root, account_name, character_name))


def make_character_name() -> str:
    suffix = "".join(chr(ord("a") + (byte % 26)) for byte in uuid.uuid4().bytes[:6])
    return f"smoke{suffix}"


def run_smoke_attempt(args: argparse.Namespace, repo_root: Path) -> int:
    game_binary = repo_root / "bin" / "ageland"
    if not game_binary.exists():
        raise RuntimeError(f"Missing game binary at {game_binary}. Build the server before running the smoke test.")

    smoke_id = f"smk{uuid.uuid4().hex[:12]}"
    account_email = f"{smoke_id}@example.com"
    character_name = make_character_name()

    temp_dir_path = Path(tempfile.mkdtemp(prefix="rots-account-smoke-"))
    capture_path = temp_dir_path / "verification-email.txt"
    capture_script_path = temp_dir_path / "capture-sendmail.sh"
    game_log_path = temp_dir_path / "game.log"
    proxy_log_path = temp_dir_path / "proxy.log"
    capture_script_path.write_text(
        "#!/bin/sh\n"
        f"cat > '{capture_path}'\n"
        "exit 0\n",
        encoding="utf-8",
    )
    capture_script_path.chmod(0o700)
    game_env = os.environ.copy()
    game_env["ROTS_SENDMAIL_COMMAND"] = str(capture_script_path)
    game_log = None
    proxy_log = None

    game_process = None
    proxy_process = None
    try:
        game_log = game_log_path.open("wb")
        game_process = subprocess.Popen(
            [str(game_binary), "-p", str(args.game_port)],
            cwd=repo_root,
            env=game_env,
            stdout=game_log,
            stderr=subprocess.STDOUT,
        )
        wait_for_port("127.0.0.1", args.game_port, args.startup_timeout)

        proxy_log = proxy_log_path.open("wb")
        proxy_process = subprocess.Popen(
            [
                "cargo",
                "run",
                "-p",
                "proxy",
                "--",
                "--game",
                f"127.0.0.1:{args.game_port}",
                "--listen",
                f"127.0.0.1:{args.proxy_port}",
                "--websocket",
                f"127.0.0.1:{args.websocket_port}",
            ],
            cwd=repo_root,
            stdout=proxy_log,
            stderr=subprocess.STDOUT,
        )
        wait_for_port("127.0.0.1", args.proxy_port, args.startup_timeout)

        with socket.create_connection(("127.0.0.1", args.proxy_port), timeout=5) as sock:
            recv_until(sock, ["Account email:"], 8.0)
            send_line(sock, account_email)

            recv_until(sock, ["Create one? (Y/N):"], 8.0)
            send_line(sock, "y")

            recv_until(sock, ["Please enter a password:"], 8.0)
            send_line(sock, DEFAULT_CREATE_PASSWORD)

            recv_until(sock, ["Please retype your password:"], 8.0)
            send_line(sock, DEFAULT_CREATE_PASSWORD)

            recv_until(sock, ["Verification code (or type RESEND/CANCEL):"], 8.0)
            verification_code = wait_for_verification_code(capture_path, args.verification_timeout)
            send_line(sock, verification_code)

            recv_until(sock, ["Choice:"], 8.0)
            send_line(sock, "1")
            recv_until(sock, ["No linked characters yet.", "Choice:"], 8.0)

            send_line(sock, "5")
            recv_until(sock, ["Current account password:"], 8.0)
            send_line(sock, DEFAULT_CREATE_PASSWORD)

            recv_until(sock, ["New account password:"], 8.0)
            send_line(sock, DEFAULT_RESET_PASSWORD)

            recv_until(sock, ["Please retype the new password:"], 8.0)
            send_line(sock, DEFAULT_RESET_PASSWORD)

            recv_until(sock, ["Account password updated.", "Choice:"], 8.0)
            send_line(sock, "0")

            recv_until(sock, ["Account email:"], 8.0)
            send_line(sock, account_email)

            recv_until(sock, ["Account password:"], 8.0)
            send_line(sock, DEFAULT_CREATE_PASSWORD)
            recv_until(sock, ["Invalid account credentials.", "Account password:"], 8.0)

            send_line(sock, DEFAULT_RESET_PASSWORD)
            recv_until(sock, ["Choice:"], 8.0)
            send_line(sock, "4")
            recv_until(sock, ["New character name:"], 8.0)
            send_line(sock, character_name)

            recv_until(sock, ["suitable name for roleplay in Middle-earth"], 8.0)
            send_line(sock, "y")

            recv_until(sock, ["Please enter a password for"], 8.0)
            send_line(sock, DEFAULT_CHARACTER_PASSWORD)

            recv_until(sock, ["Please retype your password:"], 8.0)
            send_line(sock, DEFAULT_CHARACTER_PASSWORD)

            recv_until(sock, ["What is your sex (M/F)?"], 8.0)
            send_line(sock, "m")

            recv_until(sock, ["Race:"], 8.0)
            send_line(sock, "h")

            recv_until(sock, ["Class:"], 8.0)
            send_line(sock, "a")

            recv_until(sock, ["Do you wish to enable the default colour set (Y/N)?"], 8.0)
            send_line(sock, "n")

            recv_until(sock, ["Do you see an 'a' with a pair of dots above it:"], 8.0)
            send_line(sock, "n")

            recv_until(sock, ["Make your choice:"], 8.0)
            send_line(sock, "0")

        with socket.create_connection(("127.0.0.1", args.proxy_port), timeout=5) as sock:
            recv_until(sock, ["Account email:"], 8.0)
            send_line(sock, account_email)

            recv_until(sock, ["Account password:"], 8.0)
            send_line(sock, DEFAULT_RESET_PASSWORD)

            recv_until(sock, ["Choice:"], 8.0)
            send_line(sock, "2")

            recv_until(sock, ["Character:"], 8.0)
            send_line(sock, character_name)

            recv_until(sock, ["Make your choice:"], 8.0)
            send_line(sock, "1")
            recv_until(sock, ["Here we go...", "This is your new MUD character."], 8.0)

        print(
            "Smoke test passed: create -> verify -> login -> list -> reset password -> re-login -> create character -> account-backed play succeeded."
        )
        return 0
    finally:
        terminate_process(proxy_process)
        terminate_process(game_process)
        if proxy_log is not None:
            proxy_log.close()
        if game_log is not None:
            game_log.close()

        account_name = None
        account_file = find_account_file_for_email(repo_root, account_email)
        if account_file is not None:
            account_data = read_json_file(account_file)
            if account_data is not None:
                account_name = account_data.get("account_name")

        if not args.keep_artifacts:
            cleanup_character_artifacts(repo_root, account_name, character_name)
            if account_file is not None:
                remove_if_exists(account_file)
            remove_if_exists(temp_dir_path)
        else:
            print(f"Kept smoke artifacts in {temp_dir_path}")
            if account_file is not None and account_file.exists():
                print(f"Kept temporary account file at {account_file}")
            if game_log_path.exists():
                print(f"Kept game log at {game_log_path}")
            if proxy_log_path.exists():
                print(f"Kept proxy log at {proxy_log_path}")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Smoke test the account creation, verification, login, and password reset flow through the proxy."
    )
    parser.add_argument("--repo-root", default=str(Path(__file__).resolve().parent.parent))
    parser.add_argument("--game-port", type=int, default=4001)
    parser.add_argument("--proxy-port", type=int, default=3792)
    parser.add_argument("--websocket-port", type=int, default=8082)
    parser.add_argument("--startup-timeout", type=float, default=30.0)
    parser.add_argument("--verification-timeout", type=float, default=15.0)
    parser.add_argument("--attempts", type=int, default=2)
    parser.add_argument("--keep-artifacts", action="store_true")
    args = parser.parse_args()

    repo_root = Path(args.repo_root).resolve()
    last_error: Exception | None = None
    attempts = max(args.attempts, 1)
    for attempt in range(1, attempts + 1):
        try:
            return run_smoke_attempt(args, repo_root)
        except Exception as error:
            last_error = error
            if attempt == attempts:
                raise
            print(f"Smoke attempt {attempt} failed: {error}. Retrying.", file=sys.stderr)

    if last_error is not None:
        raise last_error
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(
            "Smoke artifacts were kept in the most recent temporary directory under /tmp/rots-account-smoke-* for debugging.",
            file=sys.stderr,
        )
        print(f"Smoke test failed: {error}", file=sys.stderr)
        sys.exit(1)
