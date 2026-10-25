import importlib.util
import io
from contextlib import redirect_stdout
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

SCRIPT = Path(__file__).resolve().parents[2] / "scripts" / "mjr_recover.py"
SPEC = importlib.util.spec_from_file_location("mjr_recover", SCRIPT)
recovery = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(recovery)


def frame(payload):
    return struct.pack("<I", len(payload)) + payload


class RecoveryTests(unittest.TestCase):
    def test_complete_and_empty_containers(self):
        self.assertEqual(recovery.scan(io.BytesIO(recovery.MAGIC))["frames"], 0)
        data = recovery.MAGIC + frame(b"abc") + frame(b"defg")
        report = recovery.scan(io.BytesIO(data))
        self.assertEqual(report, {"status": "complete", "frames": 2, "good_bytes": len(data)})

    def test_all_truncation_offsets_preserve_only_complete_prefix(self):
        first = recovery.MAGIC + frame(b"abc")
        data = first + frame(b"defg")
        for offset in range(len(recovery.MAGIC), len(data) + 1):
            with self.subTest(offset=offset):
                sink = io.BytesIO()
                report = recovery.scan(io.BytesIO(data[:offset]), sink)
                expected = data if offset == len(data) else first if offset >= len(first) else recovery.MAGIC
                self.assertEqual(sink.getvalue(), expected)
                self.assertEqual(report["good_bytes"], len(expected))

    def test_bad_magic_does_not_write(self):
        for data in (b"", recovery.MAGIC[:7], b"BAD1\r\n\x1a\n"):
            sink = io.BytesIO()
            with self.assertRaises(ValueError):
                recovery.scan(io.BytesIO(data), sink)
            self.assertEqual(sink.getvalue(), b"")

    def test_zero_and_oversized_sizes_stop_without_resynchronizing(self):
        for size in (0, recovery.MAX_FRAME_BYTES + 1, 0xFFFFFFFF):
            data = recovery.MAGIC + struct.pack("<I", size) + frame(b"later")
            sink = io.BytesIO()
            report = recovery.scan(io.BytesIO(data), sink)
            self.assertEqual(report["status"], "invalid_frame_size")
            self.assertEqual(sink.getvalue(), recovery.MAGIC)

    def test_maximum_payload_is_accepted(self):
        payload = b"x" * recovery.MAX_FRAME_BYTES
        report = recovery.scan(io.BytesIO(recovery.MAGIC + frame(payload)))
        self.assertEqual(report["frames"], 1)

    def test_positive_short_reads_are_supported(self):
        class ShortReader(io.BytesIO):
            def read(self, size=-1):
                return super().read(min(size, 1))

        report = recovery.scan(ShortReader(recovery.MAGIC + frame(b"abc")))
        self.assertEqual(report["frames"], 1)

    def test_short_output_write_is_an_error(self):
        class ShortWriter(io.BytesIO):
            def write(self, data):
                return super().write(data[:-1])

        with self.assertRaises(OSError):
            recovery.scan(io.BytesIO(recovery.MAGIC), ShortWriter())

    def test_recovery_leaves_source_unchanged_and_refuses_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "source.mjr"
            output = Path(directory) / "recovered.mjr"
            data = recovery.MAGIC + frame(b"abc") + b"\x04\x00"
            source.write_bytes(data)
            report = recovery.recover(source, output)
            self.assertEqual(report["status"], "truncated_header")
            self.assertEqual(source.read_bytes(), data)
            self.assertEqual(output.read_bytes(), data[:-2])
            with self.assertRaises(FileExistsError):
                recovery.recover(source, output)
            with self.assertRaises(FileExistsError):
                recovery.recover(source, source)
            self.assertEqual(source.read_bytes(), data)
            self.assertEqual(output.read_bytes(), data[:-2])
            self.assertEqual(list(Path(directory).glob(".mjr-recovery-*")), [])

    def test_failed_publication_cleans_up_without_touching_source(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "source.mjr"
            output = Path(directory) / "recovered.mjr"
            source.write_bytes(recovery.MAGIC)
            with patch.object(recovery.os, "link", side_effect=OSError("unsupported")):
                with self.assertRaises(OSError):
                    recovery.recover(source, output)
            self.assertFalse(output.exists())
            self.assertEqual(source.read_bytes(), recovery.MAGIC)
            self.assertEqual(list(Path(directory).glob(".mjr-recovery-*")), [])

    def test_existing_symlink_is_not_followed_or_overwritten(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "source.mjr"
            output = Path(directory) / "recovered.mjr"
            source.write_bytes(recovery.MAGIC)
            output.symlink_to(source)
            with self.assertRaises(FileExistsError):
                recovery.recover(source, output)
            self.assertTrue(output.is_symlink())
            self.assertEqual(source.read_bytes(), recovery.MAGIC)

    def test_bad_magic_recovery_does_not_publish_or_leave_staging(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "source.mjr"
            output = Path(directory) / "recovered.mjr"
            source.write_bytes(b"bad")
            with self.assertRaises(ValueError):
                recovery.recover(source, output)
            self.assertFalse(output.exists())
            self.assertEqual(source.read_bytes(), b"bad")
            self.assertEqual(list(Path(directory).glob(".mjr-recovery-*")), [])

    def test_cli_inspection_and_recovery_exit_codes(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "source.mjr"
            output = Path(directory) / "recovered.mjr"
            source.write_bytes(recovery.MAGIC + frame(b"abc") + b"\x01")
            stdout = io.StringIO()
            with redirect_stdout(stdout):
                self.assertEqual(recovery.main([str(source)]), 2)
            self.assertEqual(json.loads(stdout.getvalue())["status"], "truncated_header")
            stdout = io.StringIO()
            with redirect_stdout(stdout):
                self.assertEqual(recovery.main([str(source), "--output", str(output)]), 0)
            self.assertEqual(json.loads(stdout.getvalue())["frames"], 1)
            with redirect_stdout(io.StringIO()):
                self.assertEqual(recovery.main([str(output)]), 0)

    def test_cli_invalid_header_returns_error_without_publication(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "source.mjr"
            output = Path(directory) / "recovered.mjr"
            source.write_bytes(b"bad")
            stdout = io.StringIO()
            with redirect_stdout(stdout):
                self.assertEqual(recovery.main([str(source), "--output", str(output)]), 1)
            self.assertIn("error", json.loads(stdout.getvalue()))
            self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
