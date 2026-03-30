#!/usr/bin/env python3

import importlib.util
import json
import socket
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

    def settimeout(self, _timeout: float) -> None:
        return None

    def recv(self, _size: int) -> bytes:
        if self._chunks:
            chunk = self._chunks.pop(0)
            if chunk == b"__TIMEOUT__":
                raise socket.timeout()
            return chunk
        if self._default_timeout_exception:
            raise socket.timeout()
        return b""


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


class AccountFileExpectationTest(unittest.TestCase):
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

    def test_rejects_mismatched_character_links(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            account_file = Path(temp_dir) / "account.json"
            account_file.write_text(json.dumps({"character_links": []}), encoding="utf-8")

            with self.assertRaises(RuntimeError) as context:
                account_smoke.expect_account_character_links(account_file, ["aragorn"])

            self.assertIn("Expected account character links", str(context.exception))


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
