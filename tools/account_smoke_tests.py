#!/usr/bin/env python3

import importlib.util
import io
import json
import socket
import struct
import tempfile
import unittest
from unittest import mock
from pathlib import Path


MODULE_PATH = Path(__file__).resolve().parent / "account_smoke.py"
SPEC = importlib.util.spec_from_file_location("account_smoke", MODULE_PATH)
account_smoke = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(account_smoke)


class TelnetStreamSanitizerTest(unittest.TestCase):
    def test_strips_telnet_negotiation_and_cr_nul_around_prompt(self) -> None:
        sanitizer = account_smoke.TelnetStreamSanitizer()

        text = sanitizer.feed(b"\xff\xfc\x01\r\x00Account email: ")

        self.assertEqual(text, "Account email: ")

    def test_strips_subnegotiation_sequences(self) -> None:
        sanitizer = account_smoke.TelnetStreamSanitizer()

        text = sanitizer.feed(b"Before\xff\xfa\x18\x01term-type\xff\xf0After")

        self.assertEqual(text, "BeforeAfter")

    def test_ignores_truncated_negotiation_until_followup_chunk_arrives(self) -> None:
        sanitizer = account_smoke.TelnetStreamSanitizer()

        first = sanitizer.feed(b"Verification code:\xff")
        second = sanitizer.feed(b"\xfc\x01 (or type RESEND/CANCEL):")

        self.assertEqual(first, "Verification code:")
        self.assertEqual(second, "Verification code: (or type RESEND/CANCEL):")

    def test_preserves_escaped_iac_byte(self) -> None:
        sanitizer = account_smoke.TelnetStreamSanitizer()

        text = sanitizer.feed(b"Value:\xff\xff")

        self.assertEqual(text, "Value:\xff")

    def test_does_not_manufacture_markers_from_incomplete_negotiation_noise(self) -> None:
        sanitizer = account_smoke.TelnetStreamSanitizer()

        text = sanitizer.feed(b"Cho\xff")

        self.assertEqual(text, "Cho")
        self.assertNotIn("Choice:", text)


class FakeSocket:
    def __init__(self, chunks: list[bytes], default_timeout_exception: bool = False) -> None:
        self._chunks = list(chunks)
        self._default_timeout_exception = default_timeout_exception
        self.sent_data: list[bytes] = []

    def settimeout(self, _timeout: float) -> None:
        return None

    def sendall(self, data: bytes) -> None:
        self.sent_data.append(data)

    def recv(self, _size: int) -> bytes:
        if self._chunks:
            chunk = self._chunks.pop(0)
            if chunk == b"__TIMEOUT__":
                raise socket.timeout()
            return chunk
        if self._default_timeout_exception:
            raise socket.timeout()
        return b""


class FakeProcess:
    def __init__(self, poll_results: list[int | None]) -> None:
        self._poll_results = list(poll_results)

    def poll(self) -> int | None:
        if self._poll_results:
            return self._poll_results.pop(0)
        return None


class RecvUntilTest(unittest.TestCase):
    def test_detects_prompt_after_split_telnet_negotiation(self) -> None:
        fake_socket = FakeSocket([
            b"\xff",
            b"\xfc\x01Account email: ",
        ])

        text = account_smoke.recv_until(fake_socket, ["Account email:"], 0.5)

        self.assertIn("Account email:", text)

    def test_detects_prompt_after_split_subnegotiation(self) -> None:
        fake_socket = FakeSocket([
            b"\xff\xfa\x18\x01term",
            b"-type\xff\xf0Verification code (or type RESEND/CANCEL):",
        ])

        text = account_smoke.recv_until(fake_socket, ["Verification code (or type RESEND/CANCEL):"], 0.5)

        self.assertIn("Verification code (or type RESEND/CANCEL):", text)

    def test_detects_marker_when_cr_nul_is_split_across_chunks(self) -> None:
        fake_socket = FakeSocket([
            b"Account email:\r",
            b"\x00",
        ])

        text = account_smoke.recv_until(fake_socket, ["Account email:"], 0.5)

        self.assertIn("Account email:", text)

    def test_timeout_reports_sanitized_and_raw_tails(self) -> None:
        fake_socket = FakeSocket([b"\xff\xfc\x01Acc", b"__TIMEOUT__", b""], default_timeout_exception=True)

        with self.assertRaises(RuntimeError) as context:
            account_smoke.recv_until(fake_socket, ["Account email:"], 0.1)

        message = str(context.exception)
        self.assertIn("Last sanitized output was:", message)
        self.assertIn("Raw tail was:", message)


class EmailVerificationTest(unittest.TestCase):
    def test_resends_when_initial_verification_code_expired(self) -> None:
        fake_socket = FakeSocket(
            [
                b"That verification code has expired.\r\nVerification code (or type RESEND/CANCEL): ",
                b"A new verification code has been emailed to you. It is valid for 15 minutes.\r\n"
                b"Verification code (or type RESEND/CANCEL): ",
                b"\r\nAccount: player@example.com\r\nLinked characters: 0\r\n"
                b"0) Log out\r\n5) Reset account password\r\nChoice: ",
            ]
        )
        reader = account_smoke.BufferedPromptReader(fake_socket)

        with tempfile.TemporaryDirectory() as temp_dir:
            capture_path = Path(temp_dir) / "verification-email.txt"
            capture_path.write_text("stale", encoding="utf-8")

            with mock.patch.object(account_smoke, "wait_for_verification_code", side_effect=["111111", "222222"]):
                account_smoke.complete_email_verification(fake_socket, reader, capture_path, 1.0)

            self.assertEqual(fake_socket.sent_data, [b"111111\n", b"RESEND\n", b"222222\n"])
            self.assertFalse(capture_path.exists())


class StartupWaitTest(unittest.TestCase):
    def test_reports_process_exit_with_log_tail_instead_of_port_timeout(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            log_path = Path(temp_dir) / "game.log"
            log_path.write_text("Boot db -- BEGIN.\nError opening index file 'world/scr/index'\n", encoding="utf-8")
            process = FakeProcess([1])

            with self.assertRaises(RuntimeError) as context:
                account_smoke.wait_for_process_port(process, "game server", "127.0.0.1", 4001, 1.0, log_path)

            message = str(context.exception)
            self.assertIn("game server exited with status 1 before accepting 127.0.0.1:4001", message)
            self.assertIn("world/scr/index", message)

    def test_reports_process_exit_after_refused_connect_with_log_tail(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            log_path = Path(temp_dir) / "game.log"
            log_path.write_text("Error opening index file 'world/scr/index'\n", encoding="utf-8")
            process = FakeProcess([None, 1])

            with mock.patch.object(account_smoke.socket, "create_connection", side_effect=ConnectionRefusedError):
                with mock.patch.object(account_smoke.time, "time", side_effect=[100.0, 100.0, 100.1]):
                    with mock.patch.object(account_smoke.time, "sleep"):
                        with self.assertRaises(RuntimeError) as context:
                            account_smoke.wait_for_process_port(
                                process, "game server", "127.0.0.1", 4001, 1.0, log_path
                            )

            message = str(context.exception)
            self.assertIn("game server exited with status 1 before accepting 127.0.0.1:4001", message)
            self.assertIn("world/scr/index", message)

    def test_timeout_reports_process_log_tail(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            log_path = Path(temp_dir) / "proxy.log"
            log_path.write_text("proxy startup log\n", encoding="utf-8")
            process = FakeProcess([None])

            with self.assertRaises(RuntimeError) as context:
                account_smoke.wait_for_process_port(process, "proxy", "127.0.0.1", 3792, 0.0, log_path)

            message = str(context.exception)
            self.assertIn("Timed out waiting for proxy to accept connections", message)
            self.assertIn("proxy startup log", message)

    def test_timeout_reports_last_connection_error_and_log_tail(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            log_path = Path(temp_dir) / "game.log"
            log_path.write_text("still booting\n", encoding="utf-8")
            process = FakeProcess([None, None])

            with mock.patch.object(account_smoke.socket, "create_connection", side_effect=ConnectionRefusedError):
                with mock.patch.object(account_smoke.time, "time", side_effect=[100.0, 100.0, 100.2]):
                    with mock.patch.object(account_smoke.time, "sleep"):
                        with self.assertRaises(RuntimeError) as context:
                            account_smoke.wait_for_process_port(
                                process, "game server", "127.0.0.1", 4001, 0.1, log_path
                            )

            message = str(context.exception)
            self.assertIn("Last connection error:", message)
            self.assertIn("still booting", message)

    def test_proxy_readiness_uses_log_marker_without_opening_tcp_session(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            log_path = Path(temp_dir) / "proxy.log"
            log_path.write_text("Listening for TCP connections on 127.0.0.1:3792\n", encoding="utf-8")
            process = FakeProcess([None])

            with mock.patch.object(
                account_smoke.socket,
                "create_connection",
                side_effect=AssertionError("proxy readiness should not open a TCP session"),
            ):
                account_smoke.wait_for_process_log_marker(
                    process,
                    "proxy",
                    log_path,
                    "Listening for TCP connections on 127.0.0.1:3792",
                    0.5,
                )


class SmokePortResolutionTest(unittest.TestCase):
    def test_assigns_distinct_dynamic_ports_for_zero_values(self) -> None:
        args = account_smoke.Namespace(game_port=0, proxy_port=0, websocket_port=9000)

        with mock.patch.object(account_smoke, "allocate_free_tcp_port", side_effect=[5000, 5000, 5001]):
            account_smoke.resolve_smoke_ports(args)

        self.assertEqual(args.game_port, 5000)
        self.assertEqual(args.proxy_port, 5001)
        self.assertEqual(args.websocket_port, 9000)

    def test_rejects_duplicate_explicit_ports(self) -> None:
        args = account_smoke.Namespace(game_port=4001, proxy_port=4001, websocket_port=8082)

        with self.assertRaises(RuntimeError) as context:
            account_smoke.resolve_smoke_ports(args)

        self.assertIn("must be distinct", str(context.exception))

    def test_dynamic_ports_reserve_later_explicit_ports_before_allocating(self) -> None:
        args = account_smoke.Namespace(game_port=0, proxy_port=25000, websocket_port=25001)

        with mock.patch.object(account_smoke, "allocate_free_tcp_port", side_effect=[25000, 25002]):
            account_smoke.resolve_smoke_ports(args)

        self.assertEqual(args.game_port, 25002)
        self.assertEqual(args.proxy_port, 25000)
        self.assertEqual(args.websocket_port, 25001)

    def test_rejects_game_ports_that_overflow_legacy_signed_port_handling(self) -> None:
        args = account_smoke.Namespace(game_port=40000, proxy_port=3792, websocket_port=8082)

        with self.assertRaises(RuntimeError) as context:
            account_smoke.resolve_smoke_ports(args)

        self.assertIn("game-port must be between 0 and 32767", str(context.exception))


class SmokeRetryTest(unittest.TestCase):
    def test_does_not_retry_non_retryable_startup_exit(self) -> None:
        args = account_smoke.Namespace(
            attempts=2,
            game_port=20000,
            proxy_port=20001,
            websocket_port=20002,
        )

        with mock.patch.object(
            account_smoke,
            "run_smoke_attempt",
            side_effect=account_smoke.NonRetryableSmokeError("game server exited"),
        ) as run_attempt:
            with self.assertRaises(account_smoke.NonRetryableSmokeError):
                account_smoke.run_smoke(args, Path("/repo"))

        self.assertEqual(run_attempt.call_count, 1)

    def test_retryable_prompt_timeout_gets_fresh_dynamic_ports_per_attempt(self) -> None:
        args = account_smoke.Namespace(
            attempts=2,
            game_port=0,
            proxy_port=0,
            websocket_port=0,
        )

        with mock.patch.object(
            account_smoke,
            "allocate_free_tcp_port",
            side_effect=[20000, 20001, 20002, 20010, 20011, 20012],
        ):
            with mock.patch.object(
                account_smoke,
                "run_smoke_attempt",
                side_effect=[account_smoke.RetryableSmokeError("Timed out waiting for markers Account email:"), 0],
            ) as run_attempt:
                with mock.patch.object(account_smoke.sys, "stderr", new_callable=io.StringIO):
                    result = account_smoke.run_smoke(args, Path("/repo"))

        self.assertEqual(result, 0)
        first_args = run_attempt.call_args_list[0].args[0]
        second_args = run_attempt.call_args_list[1].args[0]
        self.assertEqual((first_args.game_port, first_args.proxy_port, first_args.websocket_port), (20000, 20001, 20002))
        self.assertEqual((second_args.game_port, second_args.proxy_port, second_args.websocket_port), (20010, 20011, 20012))

    def test_does_not_retry_late_generic_prompt_timeout(self) -> None:
        args = account_smoke.Namespace(
            attempts=2,
            game_port=20000,
            proxy_port=20001,
            websocket_port=20002,
        )

        with mock.patch.object(
            account_smoke,
            "run_smoke_attempt",
            side_effect=RuntimeError("Timed out waiting for markers Choice:"),
        ) as run_attempt:
            with self.assertRaises(RuntimeError):
                account_smoke.run_smoke(args, Path("/repo"))

        self.assertEqual(run_attempt.call_count, 1)


class SmokeChildEnvironmentTest(unittest.TestCase):
    def test_child_environment_uses_allowlist_plus_explicit_values(self) -> None:
        with mock.patch.dict(
            account_smoke.os.environ,
            {
                "PATH": "/bin",
                "HOME": "/home/player",
                "LD_PRELOAD": "/tmp/inject.so",
                "AWS_SECRET_ACCESS_KEY": "secret",
            },
            clear=True,
        ):
            environment = account_smoke.smoke_child_env({"RUST_LOG": "info"})

        self.assertEqual(environment, {"PATH": "/bin", "HOME": "/home/player", "RUST_LOG": "info"})


class AccountFileExpectationTest(unittest.TestCase):
    def test_finds_only_account_json_for_email_lookup(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            account_root = repo_root / "lib" / "accounts" / "P-T" / "player@example.com"
            account_root.mkdir(parents=True)
            (account_root / "player.character.json").write_text(
                json.dumps({"normalized_email": "player@example.com"}),
                encoding="utf-8",
            )
            account_file = account_root / "account.json"
            account_file.write_text(
                json.dumps({"normalized_email": "player@example.com"}),
                encoding="utf-8",
            )

            self.assertEqual(account_smoke.find_account_file_for_email(repo_root, "player@example.com"), account_file)

    def test_accepts_expected_character_list(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            account_file = Path(temp_dir) / "account.json"
            account_file.write_text(json.dumps({"characters": ["aragorn"]}), encoding="utf-8")

            account_smoke.expect_account_character_list(account_file, ["aragorn"])

    def test_rejects_mismatched_character_list(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            account_file = Path(temp_dir) / "account.json"
            account_file.write_text(json.dumps({"characters": ["aragorn"]}), encoding="utf-8")

            with self.assertRaises(RuntimeError) as context:
                account_smoke.expect_account_character_list(account_file, [])

            self.assertIn("Expected account characters []", str(context.exception))
            self.assertIn("got ['aragorn']", str(context.exception))

    def test_accepts_expected_character_links(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            account_file = Path(temp_dir) / "account.json"
            account_file.write_text(
                json.dumps(
                    {
                        "character_links": [
                            {
                                "character_name": "aragorn",
                                "character_path": "aragorn.character.json",
                                "object_path": "aragorn.objects.json",
                                "exploits_path": "aragorn.exploits.json",
                            }
                        ]
                    }
                ),
                encoding="utf-8",
            )

            account_smoke.expect_account_character_links(account_file, ["aragorn"])

    def test_accepts_expected_account_native_character_identity_with_specialization(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            account_file = Path(temp_dir) / "account.json"
            account_file.write_text("{}", encoding="utf-8")
            character_file = Path(temp_dir) / "aragorn.character.json"
            character_file.write_text(
                json.dumps(
                    {
                        "title": "the Migrated Smoke Sentinel",
                        "progression": {"level": 7},
                        "identity": {"race": 2, "idnum": 123456},
                        "state": {"load_room": 1170, "specialization": account_smoke.LEGACY_FIXTURE_SPECIALIZATION},
                    }
                ),
                encoding="utf-8",
            )

            account_smoke.expect_account_character_identity(
                account_file,
                "Aragorn",
                7,
                2,
                123456,
                1170,
                "the Migrated Smoke Sentinel",
                account_smoke.LEGACY_FIXTURE_SPECIALIZATION,
            )

    def test_rejects_mismatched_account_native_character_specialization(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            account_file = Path(temp_dir) / "account.json"
            account_file.write_text("{}", encoding="utf-8")
            character_file = Path(temp_dir) / "aragorn.character.json"
            character_file.write_text(
                json.dumps(
                    {
                        "title": "the Migrated Smoke Sentinel",
                        "progression": {"level": 7},
                        "identity": {"race": 2, "idnum": 123456},
                        "state": {"load_room": 1170, "specialization": 0},
                    }
                ),
                encoding="utf-8",
            )

            with self.assertRaises(RuntimeError) as context:
                account_smoke.expect_account_character_identity(
                    account_file,
                    "aragorn",
                    7,
                    2,
                    123456,
                    1170,
                    "the Migrated Smoke Sentinel",
                    account_smoke.LEGACY_FIXTURE_SPECIALIZATION,
                )

            self.assertIn("'specialization': 18", str(context.exception))

    def test_accepts_expected_account_native_truecolor_foreground(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            account_file = Path(temp_dir) / "account.json"
            account_file.write_text("{}", encoding="utf-8")
            character_file = Path(temp_dir) / "aragorn.character.json"
            character_file.write_text(
                json.dumps(
                    {
                        "colors": {
                            "magic": {
                                "foreground": {
                                    "mode": "truecolor",
                                    "value": 1,
                                    "r": 12,
                                    "g": 34,
                                    "b": 56,
                                },
                                "background": {"mode": "default"},
                            }
                        }
                    }
                ),
                encoding="utf-8",
            )

            account_smoke.expect_account_character_truecolor_foreground(account_file, "Aragorn", "magic", 12, 34, 56, 1)

    def test_rejects_mismatched_account_native_truecolor_foreground(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            account_file = Path(temp_dir) / "account.json"
            account_file.write_text("{}", encoding="utf-8")
            character_file = Path(temp_dir) / "aragorn.character.json"
            character_file.write_text(
                json.dumps(
                    {
                        "colors": {
                            "magic": {
                                "foreground": {
                                    "mode": "truecolor",
                                    "value": 1,
                                    "r": 1,
                                    "g": 2,
                                    "b": 3,
                                },
                                "background": {"mode": "default"},
                            }
                        }
                    }
                ),
                encoding="utf-8",
            )

            with self.assertRaises(RuntimeError) as context:
                account_smoke.expect_account_character_truecolor_foreground(account_file, "aragorn", "magic", 12, 34, 56, 1)

            self.assertIn("Expected magic foreground", str(context.exception))

    def test_accepts_expected_migrated_object_fixture_data(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            account_file = Path(temp_dir) / "account.json"
            account_file.write_text("{}", encoding="utf-8")
            object_file = Path(temp_dir) / "aragorn.objects.json"
            object_file.write_text(
                json.dumps({"rent": {"rentcode": 1, "gold": account_smoke.LEGACY_OBJECT_GOLD}}),
                encoding="utf-8",
            )

            account_smoke.expect_account_object_fixture_data(account_file, "aragorn")

    def test_accepts_expected_migrated_exploit_fixture_data(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            account_file = Path(temp_dir) / "account.json"
            account_file.write_text("{}", encoding="utf-8")
            exploits_file = Path(temp_dir) / "aragorn.exploits.json"
            exploits_file.write_text(
                json.dumps(
                    {
                        "records": [
                            {
                                "type": 9,
                                "victim_name": account_smoke.LEGACY_EXPLOIT_VICTIM_NAME,
                                "victim_level": 7,
                                "killer_level": 8,
                                "int_param": account_smoke.LEGACY_EXPLOIT_INT_PARAM,
                            }
                        ]
                    }
                ),
                encoding="utf-8",
            )

            account_smoke.expect_account_exploit_fixture_data(account_file, "aragorn")

    def test_rejects_mismatched_character_links(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            account_file = Path(temp_dir) / "account.json"
            account_file.write_text(json.dumps({"character_links": []}), encoding="utf-8")

            with self.assertRaises(RuntimeError) as context:
                account_smoke.expect_account_character_links(account_file, ["aragorn"])

            self.assertIn("Expected account character links", str(context.exception))

    def test_cleans_flat_account_native_character_files_next_to_account_json(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            account_file = Path(temp_dir) / "account.json"
            account_file.write_text("{}", encoding="utf-8")
            character_file = Path(temp_dir) / "aragorn.character.json"
            object_file = Path(temp_dir) / "aragorn.objects.json"
            exploits_file = Path(temp_dir) / "aragorn.exploits.json"
            unrelated_file = Path(temp_dir) / "boromir.character.json"
            for path in (character_file, object_file, exploits_file, unrelated_file):
                path.write_text("{}", encoding="utf-8")

            account_smoke.cleanup_account_native_character_files(account_file, "Aragorn")

            self.assertFalse(character_file.exists())
            self.assertFalse(object_file.exists())
            self.assertFalse(exploits_file.exists())
            self.assertTrue(account_file.exists())
            self.assertTrue(unrelated_file.exists())


class LegacyPlayerFixtureTest(unittest.TestCase):
    def test_legacy_specialization_fixture_is_nonzero_weapon_mastery(self) -> None:
        self.assertEqual(account_smoke.LEGACY_FIXTURE_SPECIALIZATION, 18)
        self.assertGreater(account_smoke.LEGACY_FIXTURE_SPECIALIZATION, 0)
        self.assertEqual(
            account_smoke.legacy_fixture_specialization_line(),
            "You are specialized in weapon mastery.",
        )

    def test_encrypts_legacy_password_with_fixed_width_player_file_encoding(self) -> None:
        self.assertEqual(account_smoke.encrypt_legacy_player_password("QQtqkA0yu"), ":>n=X6/>p*")

    def test_writes_versioned_legacy_player_fixture_before_server_boot(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)

            fixture = account_smoke.write_legacy_player_fixture(repo_root, "Smokelegacy", "QQtqkA0yu")

            self.assertEqual(fixture.path.parent, repo_root / "lib" / "players" / "P-T")
            self.assertRegex(fixture.path.name, r"^smokelegacy\.7\.2\.\d+\.\d+\.0$")
            self.assertEqual(fixture.level, 7)
            self.assertEqual(fixture.race, 2)
            self.assertEqual(fixture.load_room, 1170)
            self.assertEqual(fixture.title, "the Migrated Smoke Sentinel")
            self.assertEqual(fixture.specialization, account_smoke.LEGACY_FIXTURE_SPECIALIZATION)
            fixture_text = fixture.path.read_text(encoding="latin1")
            self.assertIn("name        smokelegacy\n", fixture_text)
            self.assertIn("password    :>n=X6/>p*\n", fixture_text)
            self.assertIn("level       7\n", fixture_text)
            self.assertIn("race        2\n", fixture_text)
            self.assertIn("title       the Migrated Smoke Sentinel\n", fixture_text)
            self.assertIn("load_room   1170\n", fixture_text)
            self.assertIn(f"spec        {account_smoke.LEGACY_FIXTURE_SPECIALIZATION}\n", fixture_text)

    def test_writes_legacy_object_and_exploit_fixtures(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)

            object_path = account_smoke.write_legacy_object_fixture(repo_root, "Smokelegacy")
            exploits_path = account_smoke.write_legacy_exploits_fixture(repo_root, "Smokelegacy")

            self.assertEqual(object_path, repo_root / "lib" / "plrobjs" / "P-T" / "smokelegacy.obj")
            self.assertEqual(exploits_path, repo_root / "lib" / "exploits" / "P-T" / "smokelegacy.exploits")
            self.assertGreater(object_path.stat().st_size, 0)
            self.assertEqual(exploits_path.stat().st_size, 80)
            exploit_record = struct.unpack("<i30sh30sxxiii", exploits_path.read_bytes())
            self.assertEqual(exploit_record[0], 9)
            self.assertEqual(exploit_record[2], 42)
            self.assertEqual(exploit_record[3].split(b"\0", 1)[0].decode("ascii"), account_smoke.LEGACY_EXPLOIT_VICTIM_NAME)
            self.assertEqual(exploit_record[4:], (7, 8, account_smoke.LEGACY_EXPLOIT_INT_PARAM))

    def test_legacy_object_fixture_uses_expected_binary_sentinels(self) -> None:
        object_bytes = account_smoke.build_empty_legacy_object_bytes(account_smoke.LEGACY_OBJECT_GOLD)
        rent_format = "<iiiii hhh xx iiiii"
        rent_size = struct.calcsize(rent_format)
        object_record_size = 56
        board_points_size = 22 * struct.calcsize("<h")
        alias_terminator_size = 20
        follower_record_size = 7 * struct.calcsize("<i")

        self.assertEqual(
            len(object_bytes),
            rent_size + object_record_size + board_points_size + alias_terminator_size + follower_record_size,
        )
        rent = struct.unpack_from(rent_format, object_bytes, 0)
        self.assertEqual(rent[1:5], (1, 0, account_smoke.LEGACY_OBJECT_GOLD, 0))

        object_offset = rent_size
        self.assertEqual(struct.unpack_from("<h", object_bytes, object_offset)[0], -255)
        self.assertEqual(struct.unpack_from("<i", object_bytes, object_offset + 52)[0], -17)

        alias_offset = rent_size + object_record_size + board_points_size
        self.assertEqual(object_bytes[alias_offset : alias_offset + alias_terminator_size], bytes(alias_terminator_size))

        follower_offset = alias_offset + alias_terminator_size
        self.assertEqual(struct.unpack_from("<i", object_bytes, follower_offset)[0], -17)

    def test_make_unused_character_name_skips_existing_runtime_artifacts(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            existing_path = account_smoke.legacy_player_file_path(repo_root, "smokeaaaaaa")
            existing_path.parent.mkdir(parents=True)
            existing_path.write_text("pre-existing character", encoding="utf-8")

            with mock.patch.object(account_smoke, "make_character_name", side_effect=["smokeaaaaaa", "smokebbbbbb"]):
                self.assertEqual(account_smoke.make_unused_character_name(repo_root), "smokebbbbbb")

    def test_make_unused_character_name_skips_account_native_artifacts(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            account_root = repo_root / "lib" / "accounts" / "P-T" / "player@example.com"
            account_root.mkdir(parents=True)
            (account_root / "smokeaaaaaa.character.json").write_text("{}", encoding="utf-8")

            with mock.patch.object(account_smoke, "make_character_name", side_effect=["smokeaaaaaa", "smokebbbbbb"]):
                self.assertEqual(account_smoke.make_unused_character_name(repo_root), "smokebbbbbb")

    def test_make_unused_character_name_skips_versioned_legacy_player_artifacts(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            existing_path = account_smoke.legacy_player_file_path(repo_root, "smokeaaaaaa")
            existing_path.parent.mkdir(parents=True)
            (existing_path.parent / "smokeaaaaaa.7.2.123.456.0").write_text("pre-existing character", encoding="utf-8")

            with mock.patch.object(account_smoke, "make_character_name", side_effect=["smokeaaaaaa", "smokebbbbbb"]):
                self.assertEqual(account_smoke.make_unused_character_name(repo_root), "smokebbbbbb")

    def test_make_unused_character_name_skips_legacy_object_and_exploit_artifacts(self) -> None:
        for path_factory in (account_smoke.legacy_object_file_path, account_smoke.legacy_exploits_file_path):
            with self.subTest(path_factory=path_factory.__name__):
                with tempfile.TemporaryDirectory() as temp_dir:
                    repo_root = Path(temp_dir)
                    existing_path = path_factory(repo_root, "smokeaaaaaa")
                    existing_path.parent.mkdir(parents=True)
                    existing_path.write_text("pre-existing artifact", encoding="utf-8")

                    with mock.patch.object(account_smoke, "make_character_name", side_effect=["smokeaaaaaa", "smokebbbbbb"]):
                        self.assertEqual(account_smoke.make_unused_character_name(repo_root), "smokebbbbbb")

    def test_make_unused_character_name_skips_account_character_directory_artifacts(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            account_character_path = (
                repo_root
                / "lib"
                / "account_characters"
                / "P-T"
                / "player@example.com"
                / "smokeaaaaaa"
            )
            account_character_path.mkdir(parents=True)

            with mock.patch.object(account_smoke, "make_character_name", side_effect=["smokeaaaaaa", "smokebbbbbb"]):
                self.assertEqual(account_smoke.make_unused_character_name(repo_root), "smokebbbbbb")

    def test_cleanup_ignores_unsafe_account_name_for_account_character_directory(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            unsafe_directory = account_smoke.account_character_directory(repo_root, "../unsafe", "aragorn")
            unsafe_directory.mkdir(parents=True)
            marker = unsafe_directory / "marker"
            marker.write_text("keep", encoding="utf-8")

            account_smoke.cleanup_smoke_account_character_directory(repo_root, "../unsafe", "aragorn")

            self.assertTrue(marker.exists())


class MarkerHelperTest(unittest.TestCase):
    def test_detects_when_existing_output_already_contains_followup_marker(self) -> None:
        text = "Character 'aragorn' deleted!\n\rAccount: player@example.com\n\rLinked characters: 0\n\r0) Log out\n\r"

        self.assertTrue(account_smoke.contains_any_marker(text, ["Linked characters: 0", "0) Log out"]))

    def test_rejects_when_followup_markers_are_absent(self) -> None:
        text = "Character 'aragorn' deleted!\n\r"

        self.assertFalse(account_smoke.contains_any_marker(text, ["Linked characters: 0", "0) Log out"]))

    def test_require_markers_rejects_missing_marker(self) -> None:
        with self.assertRaises(RuntimeError) as context:
            account_smoke.require_markers("Choice:", ["0) Log out", "Choice:"], "Account menu")

        self.assertIn("missing expected markers", str(context.exception))

    def test_wait_for_account_menu_requires_all_expected_markers(self) -> None:
        fake_socket = FakeSocket([
            b"Choice:",
        ])
        reader = account_smoke.BufferedPromptReader(fake_socket)

        with self.assertRaises(RuntimeError) as context:
            account_smoke.wait_for_account_menu(reader, 0.5)

        self.assertIn("Account menu", str(context.exception))

    def test_wait_for_character_menu_rejects_generic_prompt_without_account_markers(self) -> None:
        fake_socket = FakeSocket([
            b"Make your choice:",
        ])
        reader = account_smoke.BufferedPromptReader(fake_socket)

        with self.assertRaises(RuntimeError) as context:
            account_smoke.wait_for_character_menu(reader, 0.5)

        self.assertIn("Character menu", str(context.exception))


class BufferedPromptReaderTest(unittest.TestCase):
    def test_consumes_only_through_first_matching_marker_and_keeps_remainder(self) -> None:
        fake_socket = FakeSocket([
            b"Account email: Account password: ",
        ])
        reader = account_smoke.BufferedPromptReader(fake_socket)

        first = reader.recv_until(["Account email:"], 0.5)
        second = reader.recv_until(["Account password:"], 0.5)

        self.assertIn("Account email:", first)
        self.assertIn("Account password:", second)

    def test_reuses_buffered_output_before_reading_socket_again(self) -> None:
        fake_socket = FakeSocket([
            b"Character 'aragorn' deleted!\n\rAccount: player@example.com\n\rLinked characters: 0\n\r0) Log out\n\r",
        ])
        reader = account_smoke.BufferedPromptReader(fake_socket)

        delete_output = reader.recv_until([" deleted!\n\r"], 0.5)
        menu_output = reader.recv_until(["Linked characters: 0", "0) Log out"], 0.5)

        self.assertIn("Character 'aragorn' deleted!", delete_output)
        self.assertIn("Linked characters: 0", menu_output)

    def test_detects_marker_from_final_chunk_before_timeout_boundary(self) -> None:
        fake_socket = FakeSocket([
            b"0) Log out\n\rChoice: ",
        ])
        reader = account_smoke.BufferedPromptReader(fake_socket)

        with mock.patch.object(account_smoke.time, "time", side_effect=[100.0, 100.4, 100.6]):
            text = reader.recv_until(["0) Log out"], 0.5)

        self.assertIn("0) Log out", text)


if __name__ == "__main__":
    unittest.main()
