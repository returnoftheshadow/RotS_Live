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
IAC = 255


class TelnetStreamSanitizer:
    def __init__(self) -> None:
        self._pending_iac = False
        self._pending_option = False
        self._in_subnegotiation = False
        self._subnegotiation_pending_iac = False
        self._pending_cr = False
        self._sanitized = bytearray()

    def feed(self, chunk: bytes) -> str:
        for byte in chunk:
            if self._pending_cr:
                if byte != 0:
                    self._sanitized.append(13)
                self._pending_cr = False
                if byte == 0:
                    continue

            if self._in_subnegotiation:
                if self._subnegotiation_pending_iac:
                    if byte == 240:
                        self._in_subnegotiation = False
                    self._subnegotiation_pending_iac = False
                    continue

                if byte == IAC:
                    self._subnegotiation_pending_iac = True
                continue

            if self._pending_option:
                self._pending_option = False
                continue

            if self._pending_iac:
                self._pending_iac = False
                if byte == IAC:
                    self._sanitized.append(IAC)
                    continue
                if byte in (251, 252, 253, 254):
                    self._pending_option = True
                    continue
                if byte == 250:
                    self._in_subnegotiation = True
                    self._subnegotiation_pending_iac = False
                    continue
                continue

            if byte == IAC:
                self._pending_iac = True
                continue

            if byte == 13:
                self._pending_cr = True
                continue

            self._sanitized.append(byte)

        return self.text

    @property
    def text(self) -> str:
        return self._sanitized.decode("latin1", errors="ignore")


def find_first_marker_end(text: str, markers: list[str]) -> int | None:
    matched_end = None
    matched_start = None
    for marker in markers:
        index = text.find(marker)
        if index < 0:
            continue
        marker_end = index + len(marker)
        if matched_start is None or index < matched_start or (index == matched_start and marker_end < matched_end):
            matched_start = index
            matched_end = marker_end
    return matched_end


class BufferedPromptReader:
    def __init__(self, sock: socket.socket) -> None:
        self._sock = sock
        self._sanitizer = TelnetStreamSanitizer()
        self._buffer = ""

    def recv_until(self, markers: list[str], timeout_seconds: float) -> str:
        deadline = time.time() + timeout_seconds
        raw_data = bytearray()
        self._sock.settimeout(0.5)

        while time.time() < deadline:
            marker_end = find_first_marker_end(self._buffer, markers)
            if marker_end is not None:
                text = self._buffer[:marker_end]
                self._buffer = self._buffer[marker_end:]
                return text

            try:
                chunk = self._sock.recv(4096)
            except socket.timeout:
                continue

            if not chunk:
                break

            raw_data.extend(chunk)
            previous_length = len(self._sanitizer.text)
            sanitized_text = self._sanitizer.feed(chunk)
            self._buffer += sanitized_text[previous_length:]
            marker_end = find_first_marker_end(self._buffer, markers)
            if marker_end is not None:
                text = self._buffer[:marker_end]
                self._buffer = self._buffer[marker_end:]
                return text

        marker_end = find_first_marker_end(self._buffer, markers)
        if marker_end is not None:
            text = self._buffer[:marker_end]
            self._buffer = self._buffer[marker_end:]
            return text

        text = self._buffer
        raw_tail = bytes(raw_data[-800:]).decode("latin1", errors="ignore")
        raise RuntimeError(
            "Timed out waiting for markers "
            + ", ".join(markers)
            + ". Last sanitized output was:\n"
            + text[-800:]
            + "\nRaw tail was:\n"
            + raw_tail
        )


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
    raw_data = bytearray()
    sanitizer = TelnetStreamSanitizer()
    sock.settimeout(0.5)

    while time.time() < deadline:
        try:
            chunk = sock.recv(4096)
        except socket.timeout:
            continue

        if not chunk:
            break

        raw_data.extend(chunk)
        text = sanitizer.feed(chunk)
        if any(marker in text for marker in markers):
            return text

    text = sanitizer.text
    raw_tail = bytes(raw_data[-800:]).decode("latin1", errors="ignore")
    raise RuntimeError(
        "Timed out waiting for markers "
        + ", ".join(markers)
        + ". Last sanitized output was:\n"
        + text[-800:]
        + "\nRaw tail was:\n"
        + raw_tail
    )


def contains_any_marker(text: str, markers: list[str]) -> bool:
    return any(marker in text for marker in markers)


def require_markers(text: str, markers: list[str], context: str) -> str:
    missing_markers = [marker for marker in markers if marker not in text]
    if missing_markers:
        raise RuntimeError(
            f"{context} was missing expected markers: {', '.join(missing_markers)}. Full output was:\n{text[-800:]}"
        )
    return text


def wait_for_account_menu(reader: BufferedPromptReader, timeout_seconds: float) -> str:
    text = reader.recv_until(["Choice:"], timeout_seconds)
    return require_markers(text, ["0) Log out", "5) Reset account password", "Choice:"], "Account menu")


def wait_for_character_menu(reader: BufferedPromptReader, timeout_seconds: float) -> str:
    text = reader.recv_until(["Make your choice:"], timeout_seconds)
    return require_markers(
        text,
        ["0) Back to Account Menu.", "5) Delete this character.", "Make your choice:"],
        "Character menu",
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


def expect_account_character_list(account_file: Path, expected_characters: list[str]) -> None:
    account_data = read_json_file(account_file)
    if account_data is None:
        raise RuntimeError(f"Failed to read account file at {account_file}.")

    actual_characters = account_data.get("characters")
    if actual_characters != expected_characters:
        raise RuntimeError(
            f"Expected account characters {expected_characters} in {account_file}, got {actual_characters}."
        )


def expect_account_character_links(account_file: Path, expected_characters: list[str]) -> None:
    account_data = read_json_file(account_file)
    if account_data is None:
        raise RuntimeError(f"Failed to read account file at {account_file}.")

    actual_links = account_data.get("character_links")
    expected_links = [
        {
            "character_name": character_name,
            "character_path": f"{character_name}.character.json",
            "object_path": f"{character_name}.objects.json",
            "exploits_path": f"{character_name}.exploits.json",
        }
        for character_name in expected_characters
    ]

    if actual_links != expected_links:
        raise RuntimeError(
            f"Expected account character links {expected_links} in {account_file}, got {actual_links}."
        )


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
    play_character_name = make_character_name()
    delete_character_name = make_character_name()

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
            [str(game_binary), "-x", str(args.game_port)],
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
            reader = BufferedPromptReader(sock)
            reader.recv_until(["Account email:"], 8.0)
            send_line(sock, account_email)

            reader.recv_until(["Create one? (Y/N):"], 8.0)
            send_line(sock, "y")

            reader.recv_until(["Please enter a password:"], 8.0)
            send_line(sock, DEFAULT_CREATE_PASSWORD)

            reader.recv_until(["Please retype your password:"], 8.0)
            send_line(sock, DEFAULT_CREATE_PASSWORD)

            reader.recv_until(["Verification code (or type RESEND/CANCEL):"], 8.0)
            verification_code = wait_for_verification_code(capture_path, args.verification_timeout)
            send_line(sock, verification_code)

            wait_for_account_menu(reader, 8.0)
            send_line(sock, "1")
            reader.recv_until(["No linked characters yet.", "Choice:"], 8.0)

            send_line(sock, "5")
            reader.recv_until(["Current account password:"], 8.0)
            send_line(sock, DEFAULT_CREATE_PASSWORD)

            reader.recv_until(["New account password:"], 8.0)
            send_line(sock, DEFAULT_RESET_PASSWORD)

            reader.recv_until(["Please retype the new password:"], 8.0)
            send_line(sock, DEFAULT_RESET_PASSWORD)

            reader.recv_until(["Account password updated.", "0) Log out", "Choice:"], 8.0)
            send_line(sock, "0")

            reader.recv_until(["Account email:"], 8.0)
            send_line(sock, account_email)

            reader.recv_until(["Account password:"], 8.0)
            send_line(sock, DEFAULT_CREATE_PASSWORD)
            reader.recv_until(["Invalid account credentials.", "Account password:"], 8.0)

            send_line(sock, DEFAULT_RESET_PASSWORD)
            wait_for_account_menu(reader, 8.0)
            send_line(sock, "4")
            reader.recv_until(["New character name:"], 8.0)
            send_line(sock, play_character_name)

            reader.recv_until(["suitable name for roleplay in Middle-earth"], 8.0)
            send_line(sock, "y")

            reader.recv_until(["What is your sex (M/F)?"], 8.0)
            send_line(sock, "m")

            reader.recv_until(["Race:"], 8.0)
            send_line(sock, "h")

            reader.recv_until(["Class:"], 8.0)
            send_line(sock, "a")

            reader.recv_until(["Do you wish to enable the default colour set (Y/N)?"], 8.0)
            send_line(sock, "n")

            reader.recv_until(["Do you see an 'a' with a pair of dots above it:"], 8.0)
            send_line(sock, "n")

            reader.recv_until(["Make your choice:"], 8.0)
            send_line(sock, "0")

        account_file = find_account_file_for_email(repo_root, account_email)
        if account_file is None:
            raise RuntimeError(f"Could not find account file for {account_email} after character creation.")
        expect_account_character_list(account_file, [play_character_name.lower()])
        expect_account_character_links(account_file, [play_character_name.lower()])

        with socket.create_connection(("127.0.0.1", args.proxy_port), timeout=5) as sock:
            reader = BufferedPromptReader(sock)
            reader.recv_until(["Account email:"], 8.0)
            send_line(sock, account_email)

            reader.recv_until(["Account password:"], 8.0)
            send_line(sock, DEFAULT_RESET_PASSWORD)

            wait_for_account_menu(reader, 8.0)
            send_line(sock, "2")

            reader.recv_until(["Character number:"], 8.0)
            send_line(sock, "1")

            wait_for_character_menu(reader, 8.0)
            send_line(sock, "1")
            reader.recv_until(["Here we go...", "This is your new MUD character."], 8.0)

        with socket.create_connection(("127.0.0.1", args.proxy_port), timeout=5) as sock:
            reader = BufferedPromptReader(sock)
            reader.recv_until(["Account email:"], 8.0)
            send_line(sock, account_email)

            reader.recv_until(["Account password:"], 8.0)
            send_line(sock, DEFAULT_RESET_PASSWORD)

            wait_for_account_menu(reader, 8.0)
            send_line(sock, "4")
            reader.recv_until(["New character name:"], 8.0)
            send_line(sock, delete_character_name)

            reader.recv_until(["suitable name for roleplay in Middle-earth"], 8.0)
            send_line(sock, "y")

            reader.recv_until(["What is your sex (M/F)?"], 8.0)
            send_line(sock, "m")

            reader.recv_until(["Race:"], 8.0)
            send_line(sock, "h")

            reader.recv_until(["Class:"], 8.0)
            send_line(sock, "a")

            reader.recv_until(["Do you wish to enable the default colour set (Y/N)?"], 8.0)
            send_line(sock, "n")

            reader.recv_until(["Do you see an 'a' with a pair of dots above it:"], 8.0)
            send_line(sock, "n")

            wait_for_character_menu(reader, 8.0)
            send_line(sock, "0")
            wait_for_account_menu(reader, 8.0)

            send_line(sock, "2")
            reader.recv_until(["Character number:"], 8.0)
            send_line(sock, "2")

            wait_for_character_menu(reader, 8.0)
            send_line(sock, "5")

            reader.recv_until(["Enter your account password for verification:"], 8.0)
            send_line(sock, DEFAULT_RESET_PASSWORD)

            reader.recv_until(['Please type "yes" to confirm:'], 8.0)
            send_line(sock, "yes")
            delete_output = reader.recv_until([" deleted!\n\r", " deleted!\r"], 8.0)
            account_menu_output = delete_output
            if "Choice:" not in account_menu_output:
                account_menu_output += reader.recv_until(["Choice:"], 8.0)
            require_markers(
                account_menu_output,
                ["Linked characters: 1", "0) Log out", "Choice:"],
                "Post-delete account menu",
            )

        expect_account_character_list(account_file, [play_character_name.lower()])
        expect_account_character_links(account_file, [play_character_name.lower()])
        deleted_account_root = account_file.parent
        if (deleted_account_root / f"{delete_character_name.lower()}.character.json").exists():
            raise RuntimeError("Deleted character file still exists after account-backed delete.")
        if (deleted_account_root / f"{delete_character_name.lower()}.objects.json").exists():
            raise RuntimeError("Deleted object file still exists after account-backed delete.")
        if (deleted_account_root / f"{delete_character_name.lower()}.exploits.json").exists():
            raise RuntimeError("Deleted exploits file still exists after account-backed delete.")

        with socket.create_connection(("127.0.0.1", args.proxy_port), timeout=5) as sock:
            reader = BufferedPromptReader(sock)
            reader.recv_until(["Account email:"], 8.0)
            send_line(sock, account_email)

            reader.recv_until(["Account password:"], 8.0)
            send_line(sock, DEFAULT_RESET_PASSWORD)

            wait_for_account_menu(reader, 8.0)
            send_line(sock, "1")
            final_list_output = reader.recv_until(["1 character displayed."], 8.0)
            require_markers(
                final_list_output,
                [f"1) [  1 Hum] {play_character_name[:1].upper() + play_character_name[1:].lower()}", "1 character displayed."],
                "Final linked-character list",
            )
            if delete_character_name[:1].upper() + delete_character_name[1:].lower() in final_list_output:
                raise RuntimeError("Deleted character still appeared in the final linked-character list.")

        print(
            "Smoke test passed: create -> verify -> login -> list -> reset password -> re-login -> create play character -> account-backed play -> create delete character -> delete back to account menu -> relogin-roster-check succeeded."
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
            cleanup_character_artifacts(repo_root, account_name, play_character_name)
            cleanup_character_artifacts(repo_root, account_name, delete_character_name)
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
            "Smoke artifacts were cleaned up by default; rerun with --keep-artifacts to preserve them under /tmp/rots-account-smoke-* for debugging.",
            file=sys.stderr,
        )
        print(f"Smoke test failed: {error}", file=sys.stderr)
        sys.exit(1)
