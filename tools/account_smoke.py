#!/usr/bin/env python3

import argparse
import json
import os
import random
import re
import shutil
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import time
import uuid
from argparse import Namespace
from dataclasses import dataclass
from pathlib import Path


DEFAULT_CREATE_PASSWORD = "ValidPass1"
DEFAULT_RESET_PASSWORD = "BetterPass2"
DEFAULT_CHARACTER_PASSWORD = "HeroPw1"
DEFAULT_LEGACY_CHARACTER_PASSWORD = "QQtqkA0yu"
LEGACY_FIXTURE_LEVEL = 7
LEGACY_FIXTURE_RACE = 2
LEGACY_FIXTURE_LOAD_ROOM = 1170
LEGACY_FIXTURE_TITLE = "the Migrated Smoke Sentinel"
LEGACY_OBJECT_GOLD = 4321
LEGACY_EXPLOIT_VICTIM_NAME = "legacy-smoke-victim"
LEGACY_EXPLOIT_INT_PARAM = 77
IAC = 255
LOOPBACK_HOST = "127.0.0.1"
MIN_SMOKE_PORT = 20000
MAX_GAME_PORT = 32767
VERIFICATION_CODE_PROMPT = "Verification code (or type RESEND/CANCEL):"
CHILD_ENV_ALLOWLIST = ("PATH", "HOME", "USER", "LOGNAME", "TMPDIR", "CARGO_HOME", "RUSTUP_HOME")


class NonRetryableSmokeError(RuntimeError):
    pass


class RetryableSmokeError(RuntimeError):
    pass


@dataclass(frozen=True)
class LegacyPlayerFixture:
    path: Path
    level: int
    race: int
    idnum: int
    load_room: int
    title: str


@dataclass(frozen=True)
class LegacyAssetFixtures:
    player: LegacyPlayerFixture
    object_path: Path
    exploits_path: Path


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


def read_file_tail(path: Path, max_bytes: int = 4000) -> str:
    try:
        with path.open("rb") as file:
            file.seek(0, os.SEEK_END)
            size = file.tell()
            file.seek(max(0, size - max_bytes))
            return file.read().decode("utf-8", errors="replace")
    except OSError as error:
        return f"<unable to read {path}: {error}>"


def wait_for_process_port(
    process: subprocess.Popen[bytes],
    process_name: str,
    host: str,
    port: int,
    timeout_seconds: float,
    log_path: Path,
) -> None:
    deadline = time.time() + timeout_seconds
    last_error: OSError | None = None
    while time.time() < deadline:
        exit_code = process.poll()
        if exit_code is not None:
            raise NonRetryableSmokeError(
                f"{process_name} exited with status {exit_code} before accepting {host}:{port}.\n"
                f"Last {process_name} log output:\n{read_file_tail(log_path)}"
            )

        try:
            with socket.create_connection((host, port), timeout=0.5):
                return
        except OSError as error:
            last_error = error
            time.sleep(0.1)

    exit_code = process.poll()
    if exit_code is not None:
        raise NonRetryableSmokeError(
            f"{process_name} exited with status {exit_code} before accepting {host}:{port}.\n"
            f"Last {process_name} log output:\n{read_file_tail(log_path)}"
        )

    detail = f" Last connection error: {last_error}." if last_error is not None else ""
    raise NonRetryableSmokeError(
        f"Timed out waiting for {process_name} to accept connections on {host}:{port}.{detail}\n"
        f"Last {process_name} log output:\n{read_file_tail(log_path)}"
    )


def wait_for_process_log_marker(
    process: subprocess.Popen[bytes],
    process_name: str,
    log_path: Path,
    marker: str,
    timeout_seconds: float,
) -> None:
    deadline = time.time() + timeout_seconds
    while time.time() < deadline:
        log_tail = read_file_tail(log_path)
        if marker in log_tail:
            return

        exit_code = process.poll()
        if exit_code is not None:
            raise NonRetryableSmokeError(
                f"{process_name} exited with status {exit_code} before logging readiness marker {marker!r}.\n"
                f"Last {process_name} log output:\n{log_tail}"
            )

        time.sleep(0.1)

    raise NonRetryableSmokeError(
        f"Timed out waiting for {process_name} readiness marker {marker!r}.\n"
        f"Last {process_name} log output:\n{read_file_tail(log_path)}"
    )


def allocate_free_tcp_port(
    host: str = LOOPBACK_HOST,
    minimum: int = MIN_SMOKE_PORT,
    maximum: int = MAX_GAME_PORT,
    reserved_ports: set[int] | None = None,
) -> int:
    reserved_ports = reserved_ports or set()
    candidate_ports = list(range(minimum, maximum + 1))
    random.shuffle(candidate_ports)
    for port in candidate_ports:
        if port in reserved_ports:
            continue
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
            try:
                sock.bind((host, port))
            except OSError:
                continue
            return int(sock.getsockname()[1])

    raise RuntimeError(f"Failed to find a free TCP port in {minimum}-{maximum}.")


def resolve_smoke_ports(args: Namespace) -> None:
    port_names = ("game_port", "proxy_port", "websocket_port")
    used_ports: set[int] = set()
    for port_name in port_names:
        configured_port = getattr(args, port_name)
        max_port = MAX_GAME_PORT if port_name == "game_port" else 65535
        if configured_port < 0 or configured_port > max_port:
            raise RuntimeError(f"{port_name.replace('_', '-')} must be between 0 and {max_port}.")

        if configured_port == 0:
            continue

        if configured_port in used_ports:
            raise RuntimeError(f"Smoke-test TCP ports must be distinct; {configured_port} was reused.")

        used_ports.add(configured_port)

    for port_name in port_names:
        configured_port = getattr(args, port_name)
        if configured_port != 0:
            continue

        for _ in range(100):
            configured_port = allocate_free_tcp_port(reserved_ports=used_ports)
            if configured_port not in used_ports:
                break
        else:
            raise RuntimeError("Failed to allocate distinct smoke-test TCP ports.")

        setattr(args, port_name, configured_port)
        used_ports.add(configured_port)


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


def wait_for_initial_account_email_prompt(reader: BufferedPromptReader, timeout_seconds: float) -> str:
    try:
        return reader.recv_until(["Account email:"], timeout_seconds)
    except RuntimeError as error:
        if "Timed out waiting for markers" in str(error):
            raise RetryableSmokeError(str(error)) from error
        raise


def complete_email_verification(
    sock: socket.socket,
    reader: BufferedPromptReader,
    capture_path: Path,
    timeout_seconds: float,
) -> str:
    for attempt in range(2):
        if attempt > 0:
            remove_if_exists(capture_path)
            send_line(sock, "RESEND")
            resend_output = reader.recv_until([VERIFICATION_CODE_PROMPT], 8.0)
            require_markers(
                resend_output,
                ["A new verification code has been emailed", VERIFICATION_CODE_PROMPT],
                "Verification-code resend flow",
            )

        verification_code = wait_for_verification_code(capture_path, timeout_seconds)
        send_line(sock, verification_code)

        verification_output = reader.recv_until(["Choice:", VERIFICATION_CODE_PROMPT], 8.0)
        if "That verification code has expired." in verification_output:
            continue

        return require_markers(
            verification_output,
            ["0) Log out", "5) Reset account password", "Choice:"],
            "Account menu after email verification",
        )

    raise RuntimeError("Verification code expired again after requesting a replacement code.")


def smoke_child_env(extra_values: dict[str, str]) -> dict[str, str]:
    environment = {
        key: value
        for key in CHILD_ENV_ALLOWLIST
        if (value := os.environ.get(key)) is not None
    }
    environment.update(extra_values)
    return environment


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


def is_safe_storage_component(value: str | None) -> bool:
    if not value:
        return False
    return re.fullmatch(r"[a-z0-9@._+-]+", value) is not None and ".." not in value


def find_account_file_for_email(repo_root: Path, account_email: str) -> Path | None:
    accounts_root = repo_root / "lib" / "accounts"
    if not accounts_root.exists():
        return None

    for candidate in accounts_root.rglob("account.json"):
        try:
            account_data = json.loads(candidate.read_text(encoding="utf-8"))
        except Exception:
            continue

        if not isinstance(account_data, dict):
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


def account_native_character_file(account_file: Path, character_name: str) -> Path:
    return account_file.parent / f"{character_name.strip().lower()}.character.json"


def expect_account_character_truecolor_foreground(
    account_file: Path,
    character_name: str,
    color_slot: str,
    red: int,
    green: int,
    blue: int,
    ansi_value: int,
) -> None:
    character_file = account_native_character_file(account_file, character_name)
    character_data = read_json_file(character_file)
    if character_data is None:
        raise RuntimeError(f"Failed to read account-native character file at {character_file}.")

    color_settings = character_data.get("colors")
    if not isinstance(color_settings, dict):
        raise RuntimeError(f"Expected colors object in {character_file}, got {color_settings!r}.")

    slot_settings = color_settings.get(color_slot)
    if not isinstance(slot_settings, dict):
        raise RuntimeError(f"Expected color slot {color_slot!r} in {character_file}, got {slot_settings!r}.")

    foreground = slot_settings.get("foreground")
    if not isinstance(foreground, dict):
        raise RuntimeError(f"Expected foreground object for color slot {color_slot!r} in {character_file}.")

    expected_foreground = {
        "mode": "truecolor",
        "value": ansi_value,
        "r": red,
        "g": green,
        "b": blue,
    }
    actual_foreground = {
        "mode": foreground.get("mode"),
        "value": foreground.get("value"),
        "r": foreground.get("r"),
        "g": foreground.get("g"),
        "b": foreground.get("b"),
    }
    if actual_foreground != expected_foreground:
        raise RuntimeError(
            f"Expected {color_slot} foreground {expected_foreground} in {character_file}, got {actual_foreground}."
        )

    background = slot_settings.get("background")
    if not isinstance(background, dict) or background.get("mode") != "default":
        raise RuntimeError(f"Expected {color_slot} background to be default in {character_file}, got {background!r}.")


def expect_account_native_character_assets(account_file: Path, character_name: str) -> None:
    normalized_character = character_name.strip().lower()
    for suffix in ("character", "objects", "exploits"):
        asset_path = account_file.parent / f"{normalized_character}.{suffix}.json"
        if not asset_path.exists():
            raise RuntimeError(f"Expected account-native {suffix} asset at {asset_path}.")


def expect_account_native_character_assets_absent(account_file: Path, character_name: str) -> None:
    normalized_character = character_name.strip().lower()
    for suffix in ("character", "objects", "exploits"):
        asset_path = account_file.parent / f"{normalized_character}.{suffix}.json"
        if asset_path.exists():
            raise RuntimeError(f"Unexpected account-native {suffix} asset at {asset_path}.")


def expect_account_object_fixture_data(account_file: Path, character_name: str) -> None:
    object_file = account_file.parent / f"{character_name.strip().lower()}.objects.json"
    object_data = read_json_file(object_file)
    if object_data is None:
        raise RuntimeError(f"Failed to read account-native object file at {object_file}.")

    rent = object_data.get("rent")
    if not isinstance(rent, dict) or rent.get("gold") != LEGACY_OBJECT_GOLD or rent.get("rentcode") != 1:
        raise RuntimeError(f"Expected migrated object rent data with gold {LEGACY_OBJECT_GOLD} in {object_file}, got {rent!r}.")


def expect_account_exploit_fixture_data(account_file: Path, character_name: str) -> None:
    exploits_file = account_file.parent / f"{character_name.strip().lower()}.exploits.json"
    exploit_data = read_json_file(exploits_file)
    if exploit_data is None:
        raise RuntimeError(f"Failed to read account-native exploits file at {exploits_file}.")

    records = exploit_data.get("records")
    if not isinstance(records, list) or len(records) != 1 or not isinstance(records[0], dict):
        raise RuntimeError(f"Expected one migrated exploit record in {exploits_file}, got {records!r}.")

    record = records[0]
    expected = {
        "type": 9,
        "victim_name": LEGACY_EXPLOIT_VICTIM_NAME,
        "victim_level": 7,
        "killer_level": 8,
        "int_param": LEGACY_EXPLOIT_INT_PARAM,
    }
    actual = {
        "type": record.get("type"),
        "victim_name": record.get("victim_name"),
        "victim_level": record.get("victim_level"),
        "killer_level": record.get("killer_level"),
        "int_param": record.get("int_param"),
    }
    if actual != expected:
        raise RuntimeError(f"Expected migrated exploit record {expected} in {exploits_file}, got {actual}.")


def expect_account_character_identity(
    account_file: Path,
    character_name: str,
    expected_level: int,
    expected_race: int,
    expected_idnum: int,
    expected_load_room: int,
    expected_title: str,
) -> None:
    character_file = account_native_character_file(account_file, character_name)
    character_data = read_json_file(character_file)
    if character_data is None:
        raise RuntimeError(f"Failed to read account-native character file at {character_file}.")

    progression = character_data.get("progression")
    identity = character_data.get("identity")
    state = character_data.get("state")
    actual = {
        "title": character_data.get("title"),
        "level": progression.get("level") if isinstance(progression, dict) else None,
        "race": identity.get("race") if isinstance(identity, dict) else None,
        "idnum": identity.get("idnum") if isinstance(identity, dict) else None,
        "load_room": state.get("load_room") if isinstance(state, dict) else None,
    }
    expected = {
        "title": expected_title,
        "level": expected_level,
        "race": expected_race,
        "idnum": expected_idnum,
        "load_room": expected_load_room,
    }
    if actual != expected:
        raise RuntimeError(f"Expected migrated character identity {expected} in {character_file}, got {actual}.")


def character_bucket_for_name(name: str) -> str:
    return account_bucket_for_name(name)


def legacy_player_file_path(repo_root: Path, character_name: str) -> Path:
    normalized = character_name.strip().lower()
    return repo_root / "lib" / "players" / character_bucket_for_name(normalized) / normalized


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


def cleanup_account_native_character_files(account_file: Path, character_name: str) -> None:
    if account_file.name != "account.json":
        return

    normalized_character = character_name.strip().lower()
    for suffix in ("character", "objects", "exploits"):
        remove_if_exists(account_file.parent / f"{normalized_character}.{suffix}.json")


def cleanup_smoke_account_character_directory(repo_root: Path, account_name: str | None, character_name: str) -> None:
    if account_name and is_safe_storage_component(account_name):
        remove_if_exists(account_character_directory(repo_root, account_name, character_name))


def cleanup_legacy_asset_fixtures(fixtures: LegacyAssetFixtures | None) -> None:
    if fixtures is None:
        return

    remove_if_exists(fixtures.player.path)
    remove_if_exists(fixtures.object_path)
    remove_if_exists(fixtures.exploits_path)


def remove_empty_directory(path: Path) -> None:
    try:
        path.rmdir()
    except OSError:
        return


def character_artifacts_exist(repo_root: Path, character_name: str) -> bool:
    canonical_path = legacy_player_file_path(repo_root, character_name)
    if canonical_path.exists() or any(canonical_path.parent.glob(f"{canonical_path.name}.*")):
        return True
    if legacy_object_file_path(repo_root, character_name).exists():
        return True
    if legacy_exploits_file_path(repo_root, character_name).exists():
        return True

    normalized_character = character_name.strip().lower()
    accounts_root = repo_root / "lib" / "accounts"
    if accounts_root.exists() and any(accounts_root.rglob(f"{normalized_character}.*.json")):
        return True

    account_characters_root = repo_root / "lib" / "account_characters"
    if account_characters_root.exists() and any(account_characters_root.rglob(normalized_character)):
        return True

    return False


def make_character_name() -> str:
    suffix = "".join(chr(ord("a") + (byte % 26)) for byte in uuid.uuid4().bytes[:6])
    return f"smoke{suffix}"


def make_unused_character_name(repo_root: Path) -> str:
    for _ in range(100):
        candidate = make_character_name()
        if not character_artifacts_exist(repo_root, candidate):
            return candidate
    raise RuntimeError("Failed to find an unused smoke character name after 100 attempts.")


def encrypt_legacy_player_password(password: str, max_length: int = 10) -> str:
    password_bytes = list(password.encode("latin1")[:max_length])
    password_bytes.extend([0] * (max_length - len(password_bytes)))
    for index, value in enumerate(password_bytes):
        if value > 127:
            password_bytes[index] = value - 128

    encrypted_bytes = []
    for index in range(max_length - 1):
        key1 = password_bytes[index] * 16
        key2 = password_bytes[index + 1] // 8
        encrypted_bytes.append(((key1 + key2) & 127) + 32)

    key1 = password_bytes[max_length - 1] * 16
    key2 = password_bytes[0] // 8
    encrypted_bytes.append(((key1 + key2) & 127) + 32)
    return bytes(encrypted_bytes).decode("latin1")


def write_legacy_player_fixture(repo_root: Path, character_name: str, password: str) -> LegacyPlayerFixture:
    normalized_character = character_name.strip().lower()
    legacy_bucket_path = legacy_player_file_path(repo_root, normalized_character).parent
    legacy_bucket_path.mkdir(parents=True, exist_ok=True)

    level = LEGACY_FIXTURE_LEVEL
    race = LEGACY_FIXTURE_RACE
    idnum = random.randint(100000, 999999)
    last_logon = int(time.time())
    flags = 0
    fixture_path = legacy_bucket_path / f"{normalized_character}.{level}.{race}.{idnum}.{last_logon}.{flags}"
    encrypted_password = encrypt_legacy_player_password(password)
    fixture_text = (
        "#player\n"
        "version     1\n"
        f"name        {normalized_character}\n"
        "sex         1\n"
        "prof        0\n"
        f"race        {race}\n"
        "bodytype    1\n"
        f"level       {level}\n"
        "language    1\n"
        f"birth       {last_logon}\n"
        "played      0\n"
        "weight      180\n"
        "height      70\n"
        f"title       {LEGACY_FIXTURE_TITLE}\n"
        "hometown    1\n"
        "description \n"
        "A legacy smoke-test character.\n"
        "~\n"
        f"last_logon  {last_logon}\n"
        f"password    {encrypted_password}\n"
        "host        smoke-test\n"
        f"idnum       {idnum}\n"
        f"load_room   {LEGACY_FIXTURE_LOAD_ROOM}\n"
        "sp_to_learn 0\n"
        "alignment   0\n"
        "act         0\n"
        "pref        0\n"
        "wimpy       10\n"
        "freeze_lvl  0\n"
        "bad_pws     0\n"
        "conditions0 0\n"
        "conditions1 0\n"
        "conditions2 0\n"
        "mini_lvl    0\n"
        "morale      0\n"
        "owner       0\n"
        "rerolls     0\n"
        "max_mini_lv 0\n"
        "perception  0\n"
        "rp_flag     0\n"
        "retiredon   0\n"
        "tactics     3\n"
        "shooting    2\n"
        "casting     2\n"
        "twohanded   0\n"
        "ob          0\n"
        "damage      0\n"
        "ENE_regen   0\n"
        "parry       0\n"
        "dodge       0\n"
        "gold        0\n"
        "exp         0\n"
        "encumb      0\n"
        "spec        0\n"
        "tmpstats    10 10 10 10 10 10\n"
        "tmpabil     20 20 100 0\n"
        "permstats    10 10 10 10 10 10\n"
        "permabil     20 20 100 0\n"
        "prof_coef   0 0\n"
        "prof_coef   1 0\n"
        "prof_coef   2 0\n"
        "prof_coef   3 0\n"
        "prof_coef   4 0\n"
        "prof_level  0 0\n"
        "prof_level  1 0\n"
        "prof_level  2 0\n"
        "prof_level  3 0\n"
        "prof_level  4 0\n"
        "prof_exp    0 0\n"
        "prof_exp    1 0\n"
        "prof_exp    2 0\n"
        "prof_exp    3 0\n"
        "prof_exp    4 0\n"
        "end\n"
    )
    fixture_path.write_text(fixture_text, encoding="latin1")
    return LegacyPlayerFixture(
        path=fixture_path,
        level=level,
        race=race,
        idnum=idnum,
        load_room=LEGACY_FIXTURE_LOAD_ROOM,
        title=LEGACY_FIXTURE_TITLE,
    )


def build_empty_legacy_object_bytes(rent_gold: int) -> bytes:
    rent_info = struct.pack(
        "<iiiii hhh xx iiiii",
        int(time.time()),
        1,
        0,
        rent_gold,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
    )
    object_sentinel = (
        struct.pack("<h5hiiii", -255, 0, 0, 0, 0, 0, 0, 0, 0, 0)
        + struct.pack("<Bxxxi", 0, 0)
        + struct.pack("<Bxxxi", 0, 0)
        + struct.pack("<hxxii", 0, 0, -17)
    )
    board_points = struct.pack("<22h", *([0] * 22))
    alias_terminator = bytes(20)
    follower_sentinel = struct.pack("<iiiiiii", -17, 0, 0, 0, 0, 0, 0)
    return rent_info + object_sentinel + board_points + alias_terminator + follower_sentinel


def write_legacy_object_fixture(repo_root: Path, character_name: str) -> Path:
    object_path = legacy_object_file_path(repo_root, character_name)
    object_path.parent.mkdir(parents=True, exist_ok=True)
    object_path.write_bytes(build_empty_legacy_object_bytes(LEGACY_OBJECT_GOLD))
    return object_path


def build_legacy_exploit_bytes() -> bytes:
    timestamp = b"Mon Jan  1 00:00:00 2024"
    victim_name = LEGACY_EXPLOIT_VICTIM_NAME.encode("ascii")
    return struct.pack(
        "<i30sh30sxxiii",
        9,
        timestamp + bytes(30 - len(timestamp)),
        42,
        victim_name + bytes(30 - len(victim_name)),
        7,
        8,
        LEGACY_EXPLOIT_INT_PARAM,
    )


def write_legacy_exploits_fixture(repo_root: Path, character_name: str) -> Path:
    exploits_path = legacy_exploits_file_path(repo_root, character_name)
    exploits_path.parent.mkdir(parents=True, exist_ok=True)
    exploits_path.write_bytes(build_legacy_exploit_bytes())
    return exploits_path


def write_legacy_asset_fixtures(repo_root: Path, character_name: str, password: str) -> LegacyAssetFixtures:
    return LegacyAssetFixtures(
        player=write_legacy_player_fixture(repo_root, character_name, password),
        object_path=write_legacy_object_fixture(repo_root, character_name),
        exploits_path=write_legacy_exploits_fixture(repo_root, character_name),
    )


def run_smoke_attempt(args: argparse.Namespace, repo_root: Path) -> int:
    game_binary = repo_root / "bin" / "ageland"
    if not game_binary.exists():
        raise RuntimeError(f"Missing game binary at {game_binary}. Build the server before running the smoke test.")

    smoke_id = f"smk{uuid.uuid4().hex[:12]}"
    account_email = f"{smoke_id}@example.com"
    generated_character_names: set[str] = set()
    play_character_name = make_unused_character_name(repo_root)
    generated_character_names.add(play_character_name)
    legacy_character_name = make_unused_character_name(repo_root)
    while legacy_character_name in generated_character_names:
        legacy_character_name = make_unused_character_name(repo_root)
    generated_character_names.add(legacy_character_name)
    delete_character_name = make_unused_character_name(repo_root)
    while delete_character_name in generated_character_names:
        delete_character_name = make_unused_character_name(repo_root)
    legacy_asset_fixtures: LegacyAssetFixtures | None = None

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
    game_env = smoke_child_env({"ROTS_SENDMAIL_COMMAND": str(capture_script_path)})
    game_log = None
    proxy_log = None

    game_process = None
    proxy_process = None
    passed = False
    try:
        legacy_asset_fixtures = write_legacy_asset_fixtures(
            repo_root,
            legacy_character_name,
            DEFAULT_LEGACY_CHARACTER_PASSWORD,
        )

        game_log = game_log_path.open("wb")
        game_process = subprocess.Popen(
            [str(game_binary), "-x", str(args.game_port)],
            cwd=repo_root,
            env=game_env,
            stdout=game_log,
            stderr=subprocess.STDOUT,
        )
        wait_for_process_port(
            game_process,
            "game server",
            LOOPBACK_HOST,
            args.game_port,
            args.startup_timeout,
            game_log_path,
        )

        proxy_log = proxy_log_path.open("wb")
        proxy_env = smoke_child_env({"RUST_LOG": "info"})
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
            env=proxy_env,
            stdout=proxy_log,
            stderr=subprocess.STDOUT,
        )
        wait_for_process_log_marker(
            proxy_process,
            "proxy",
            proxy_log_path,
            f"Listening for TCP connections on {LOOPBACK_HOST}:{args.proxy_port}",
            args.startup_timeout,
        )

        with socket.create_connection((LOOPBACK_HOST, args.proxy_port), timeout=5) as sock:
            reader = BufferedPromptReader(sock)
            wait_for_initial_account_email_prompt(reader, 8.0)
            send_line(sock, account_email)

            reader.recv_until(["Create one? (Y/N):"], 8.0)
            send_line(sock, "y")

            reader.recv_until(["Please enter a password:"], 8.0)
            send_line(sock, DEFAULT_CREATE_PASSWORD)

            reader.recv_until(["Please retype your password:"], 8.0)
            send_line(sock, DEFAULT_CREATE_PASSWORD)

            reader.recv_until([VERIFICATION_CODE_PROMPT], 8.0)
            complete_email_verification(sock, reader, capture_path, args.verification_timeout)
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

        with socket.create_connection((LOOPBACK_HOST, args.proxy_port), timeout=5) as sock:
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
            send_line(sock, "color on")
            reader.recv_until(["Colours turned on."], 8.0)

            send_line(sock, "color magic fg rgb 12 34 56")
            reader.recv_until(["You set magic foreground to #0C2238."], 8.0)

            send_line(sock, "color magic fg rgb 999 34 56")
            reader.recv_until(["RGB values must be between 0 and 255."], 8.0)

            send_line(sock, "quit")
            reader.recv_until(["As you quit, all your posessions drop to the ground!", "Goodbye"], 8.0)

        expect_account_character_truecolor_foreground(account_file, play_character_name, "magic", 12, 34, 56, 0)

        with socket.create_connection((LOOPBACK_HOST, args.proxy_port), timeout=5) as sock:
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

            send_line(sock, "color")
            color_output = reader.recv_until(["weather:"], 8.0)
            require_markers(
                color_output,
                ["Your colours are:", "magic: fg #0C2238 bg default"],
                "Persisted account-backed color listing",
            )

            send_line(sock, "quit")
            reader.recv_until(["As you quit, all your posessions drop to the ground!", "Goodbye"], 8.0)

        with socket.create_connection((LOOPBACK_HOST, args.proxy_port), timeout=5) as sock:
            reader = BufferedPromptReader(sock)
            reader.recv_until(["Account email:"], 8.0)
            send_line(sock, account_email)

            reader.recv_until(["Account password:"], 8.0)
            send_line(sock, DEFAULT_RESET_PASSWORD)

            wait_for_account_menu(reader, 8.0)
            send_line(sock, "3")
            reader.recv_until(["Legacy character name:"], 8.0)
            send_line(sock, legacy_character_name)

            reader.recv_until(["Legacy character password:"], 8.0)
            send_line(sock, "WrongLegacy")
            failed_link_output = reader.recv_until(["Choice:"], 8.0)
            require_markers(
                failed_link_output,
                ["Incorrect legacy character password.", "Linked characters: 1", "Choice:"],
                "Failed legacy character link flow",
            )

            expect_account_character_list(account_file, [play_character_name.lower()])
            expect_account_character_links(account_file, [play_character_name.lower()])
            expect_account_native_character_assets_absent(account_file, legacy_character_name)
            if legacy_asset_fixtures is None or not legacy_asset_fixtures.player.path.exists():
                raise RuntimeError("Legacy player fixture was retired before successful migration.")
            if not legacy_asset_fixtures.object_path.exists() or not legacy_asset_fixtures.exploits_path.exists():
                raise RuntimeError("Legacy object/exploit fixtures were retired before successful migration.")

            send_line(sock, "3")
            reader.recv_until(["Legacy character name:"], 8.0)
            send_line(sock, legacy_character_name)

            reader.recv_until(["Legacy character password:"], 8.0)
            send_line(sock, DEFAULT_LEGACY_CHARACTER_PASSWORD)
            link_output = reader.recv_until(["Choice:"], 8.0)
            require_markers(
                link_output,
                [
                    f"Successfully added {legacy_character_name[:1].upper() + legacy_character_name[1:].lower()} to your account.",
                    "Linked characters: 2",
                    "Choice:",
                ],
                "Legacy character link flow",
            )

            expect_account_character_list(account_file, [play_character_name.lower(), legacy_character_name.lower()])
            expect_account_character_links(account_file, [play_character_name.lower(), legacy_character_name.lower()])
            expect_account_native_character_assets(account_file, legacy_character_name)
            if legacy_asset_fixtures is None:
                raise RuntimeError("Legacy asset fixture metadata was not available after migration.")
            expect_account_character_identity(
                account_file,
                legacy_character_name,
                legacy_asset_fixtures.player.level,
                legacy_asset_fixtures.player.race,
                legacy_asset_fixtures.player.idnum,
                legacy_asset_fixtures.player.load_room,
                legacy_asset_fixtures.player.title,
            )
            expect_account_object_fixture_data(account_file, legacy_character_name)
            expect_account_exploit_fixture_data(account_file, legacy_character_name)
            for retired_path in (
                legacy_asset_fixtures.player.path,
                legacy_asset_fixtures.object_path,
                legacy_asset_fixtures.exploits_path,
            ):
                if retired_path.exists():
                    raise RuntimeError(f"Legacy fixture file still exists after account migration: {retired_path}")

            send_line(sock, "2")
            reader.recv_until(["Character number:"], 8.0)
            send_line(sock, "2")

            wait_for_character_menu(reader, 8.0)
            send_line(sock, "1")
            reader.recv_until(["Here we go..."], 8.0)

            send_line(sock, "info")
            info_output = reader.recv_until([f"You have reached level {LEGACY_FIXTURE_LEVEL}."], 8.0)
            require_markers(
                info_output,
                [LEGACY_FIXTURE_TITLE, f"You have reached level {LEGACY_FIXTURE_LEVEL}."],
                "Migrated character live info",
            )

            send_line(sock, "quit")
            reader.recv_until(["As you quit, all your posessions drop to the ground!", "Goodbye"], 8.0)

        expect_account_character_list(account_file, [play_character_name.lower(), legacy_character_name.lower()])
        expect_account_character_links(account_file, [play_character_name.lower(), legacy_character_name.lower()])
        expect_account_native_character_assets(account_file, legacy_character_name)

        with socket.create_connection((LOOPBACK_HOST, args.proxy_port), timeout=5) as active_sock:
            active_reader = BufferedPromptReader(active_sock)
            active_reader.recv_until(["Account email:"], 8.0)
            send_line(active_sock, account_email)

            active_reader.recv_until(["Account password:"], 8.0)
            send_line(active_sock, DEFAULT_RESET_PASSWORD)

            wait_for_account_menu(active_reader, 8.0)
            send_line(active_sock, "2")

            active_reader.recv_until(["Character number:"], 8.0)
            send_line(active_sock, "1")

            wait_for_character_menu(active_reader, 8.0)
            send_line(active_sock, "1")
            active_reader.recv_until(["Here we go..."], 8.0)

            with socket.create_connection((LOOPBACK_HOST, args.proxy_port), timeout=5) as second_sock:
                second_reader = BufferedPromptReader(second_sock)
                second_reader.recv_until(["Account email:"], 8.0)
                send_line(second_sock, account_email)

                second_reader.recv_until(["Account password:"], 8.0)
                send_line(second_sock, DEFAULT_RESET_PASSWORD)

                second_menu = wait_for_account_menu(second_reader, 8.0)
                require_markers(
                    second_menu,
                    [
                        f"Active character: {play_character_name[:1].upper() + play_character_name[1:].lower()}",
                    ],
                    "Second account login active-character menu",
                )
                send_line(second_sock, "2")

                second_reader.recv_until(["Character number:"], 8.0)
                send_line(second_sock, "2")
                blocked_selection = second_reader.recv_until(["Character number:"], 8.0)
                require_markers(
                    blocked_selection,
                    [
                        f"You are already connected as {play_character_name[:1].upper() + play_character_name[1:].lower()}.",
                        "Linked characters for your account:",
                        "Character number:",
                    ],
                    "Second account login blocked different-character selection",
                )

                send_line(second_sock, "1")
                usurp_output = second_reader.recv_until(["You take over your own body, already in use!"], 8.0)
                require_markers(
                    usurp_output,
                    ["You take over your own body, already in use!"],
                    "Second account login same-character reconnect",
                )
                send_line(second_sock, "quit")
                second_reader.recv_until(["As you quit, all your posessions drop to the ground!", "Goodbye"], 8.0)

        with socket.create_connection((LOOPBACK_HOST, args.proxy_port), timeout=5) as sock:
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

            expect_account_native_character_assets(account_file, delete_character_name)

            send_line(sock, "2")
            reader.recv_until(["Character number:"], 8.0)
            send_line(sock, "3")

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
                ["Linked characters: 2", "0) Log out", "Choice:"],
                "Post-delete account menu",
            )

        expect_account_character_list(account_file, [play_character_name.lower(), legacy_character_name.lower()])
        expect_account_character_links(account_file, [play_character_name.lower(), legacy_character_name.lower()])
        deleted_account_root = account_file.parent
        if (deleted_account_root / f"{delete_character_name.lower()}.character.json").exists():
            raise RuntimeError("Deleted character file still exists after account-backed delete.")
        if (deleted_account_root / f"{delete_character_name.lower()}.objects.json").exists():
            raise RuntimeError("Deleted object file still exists after account-backed delete.")
        if (deleted_account_root / f"{delete_character_name.lower()}.exploits.json").exists():
            raise RuntimeError("Deleted exploits file still exists after account-backed delete.")

        with socket.create_connection((LOOPBACK_HOST, args.proxy_port), timeout=5) as sock:
            reader = BufferedPromptReader(sock)
            reader.recv_until(["Account email:"], 8.0)
            send_line(sock, account_email)

            reader.recv_until(["Account password:"], 8.0)
            send_line(sock, DEFAULT_RESET_PASSWORD)

            wait_for_account_menu(reader, 8.0)
            send_line(sock, "1")
            final_list_output = reader.recv_until(["2 characters displayed."], 8.0)
            require_markers(
                final_list_output,
                [
                    play_character_name[:1].upper() + play_character_name[1:].lower(),
                    legacy_character_name[:1].upper() + legacy_character_name[1:].lower(),
                    "2 characters displayed.",
                ],
                "Final linked-character list",
            )
            if delete_character_name[:1].upper() + delete_character_name[1:].lower() in final_list_output:
                raise RuntimeError("Deleted character still appeared in the final linked-character list.")

        print(
            "Smoke test passed: create -> verify -> login -> list -> reset password -> re-login -> create play character -> account-backed play -> save color persistence -> relogin color verification -> link legacy character -> second-login active-character guard -> account-backed legacy play -> create delete character -> delete back to account menu -> relogin-roster-check succeeded."
        )
        passed = True
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
            cleanup_legacy_asset_fixtures(legacy_asset_fixtures)
            cleanup_smoke_account_character_directory(repo_root, account_name, play_character_name)
            cleanup_smoke_account_character_directory(repo_root, account_name, legacy_character_name)
            cleanup_smoke_account_character_directory(repo_root, account_name, delete_character_name)
            if account_file is not None:
                cleanup_account_native_character_files(account_file, play_character_name)
                cleanup_account_native_character_files(account_file, legacy_character_name)
                cleanup_account_native_character_files(account_file, delete_character_name)
                remove_if_exists(account_file)
                remove_empty_directory(account_file.parent)

        if not args.keep_artifacts and passed:
            remove_if_exists(temp_dir_path)
        else:
            print(f"Kept smoke artifacts in {temp_dir_path}")
            if args.keep_artifacts and account_file is not None and account_file.exists():
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
    parser.add_argument("--game-port", type=int, default=0, help="Game TCP port; use 0 to choose a free port.")
    parser.add_argument("--proxy-port", type=int, default=0, help="Proxy TCP port; use 0 to choose a free port.")
    parser.add_argument("--websocket-port", type=int, default=0, help="Proxy WebSocket port; use 0 to choose a free port.")
    parser.add_argument("--startup-timeout", type=float, default=30.0)
    parser.add_argument("--verification-timeout", type=float, default=15.0)
    parser.add_argument("--attempts", type=int, default=2)
    parser.add_argument("--keep-artifacts", action="store_true")
    args = parser.parse_args()

    repo_root = Path(args.repo_root).resolve()
    return run_smoke(args, repo_root)


def is_retryable_smoke_error(error: Exception) -> bool:
    return isinstance(error, RetryableSmokeError)


def run_smoke(args: Namespace, repo_root: Path) -> int:
    last_error: Exception | None = None
    attempts = max(args.attempts, 1)
    for attempt in range(1, attempts + 1):
        attempt_args = Namespace(**vars(args))
        resolve_smoke_ports(attempt_args)
        try:
            return run_smoke_attempt(attempt_args, repo_root)
        except Exception as error:
            last_error = error
            if attempt == attempts or not is_retryable_smoke_error(error):
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
            "Successful smoke artifacts are cleaned up by default; failed attempts preserve artifacts under /tmp/rots-account-smoke-* for debugging.",
            file=sys.stderr,
        )
        print(f"Smoke test failed: {error}", file=sys.stderr)
        sys.exit(1)
