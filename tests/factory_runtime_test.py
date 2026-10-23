"""Synthetic serial checks: fixed projection, console prompts and failed boots."""
import contextlib
import io
from pathlib import Path
import sys
import types
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from deploy_factory import verify_runtime


class SerialFixture:
  def __init__(self, payload):
    self.payload = payload

  def __enter__(self):
    return self

  def __exit__(self, *_):
    return False

  def read(self, size):
    chunk, self.payload = self.payload[:size], self.payload[size:]
    return chunk


class FactoryRuntimeTest(unittest.TestCase):
  def run_protocol(self, payload, self_test, timestamps):
    serial = types.SimpleNamespace(Serial=lambda *_, **__: SerialFixture(payload))
    output = io.StringIO()
    with patch.dict(sys.modules, {'serial': serial}), \
         patch('deploy_factory.time.sleep'), \
         patch('deploy_factory.time.monotonic', side_effect=timestamps), \
         contextlib.redirect_stdout(output):
      verify_runtime('synthetic-port', self_test)
    return output.getvalue()

  def test_console_prompt_and_private_noise(self):
    lines = [
      b'SYNTHETIC_PRIVATE_VALUE',
      b'\x1b[0;32mSenseCAP>\x1b[0m NATIVE_FACTORY_MENU_READY_V2',
    ]
    for _ in range(2):
      lines += [
        b'NATIVE_SPECTRUM_ENTERED_V1',
        b'NATIVE_SPECTRUM_RUNNING_V1',
        b'NATIVE_SPECTRUM_EXITED_V1',
      ]
    lines.append(b'NATIVE_LIFECYCLE_SELF_TEST_PASS_V1')
    output = self.run_protocol(b'\r\n'.join(lines) + b'\r\n', True, [0, 1])
    self.assertIn('FACTORY_MENU_LIFECYCLE_VERIFIED', output)
    self.assertEqual(output.count('NATIVE_SPECTRUM_ENTERED_V1'), 2)
    self.assertNotIn('SYNTHETIC_PRIVATE_VALUE', output)
    self.assertNotIn('SenseCAP>', output)
    self.assertNotIn('\x1b', output)

  def test_normal_boot_observation_window(self):
    output = self.run_protocol(b'NATIVE_FACTORY_MENU_READY_V2\n', False, [0, 1, 36])
    self.assertIn('FACTORY_MENU_BOOT_VERIFIED', output)

  def test_repeated_boot_is_not_stable(self):
    with self.assertRaisesRegex(RuntimeError, '^FACTORY_RUNTIME_UNCONFIRMED$'):
      self.run_protocol(b'NATIVE_FACTORY_MENU_READY_V2\n' * 2, False, [0, 1, 36])

  def test_watchdog_cannot_pass_after_menu_ready(self):
    with self.assertRaisesRegex(RuntimeError, '^FACTORY_RUNTIME_ERROR$'):
      self.run_protocol(b'NATIVE_FACTORY_MENU_READY_V2\nTask watchdog got triggered\n',
                        False, [0, 1])

  def test_console_does_not_hide_a_panic(self):
    with self.assertRaisesRegex(RuntimeError, '^FACTORY_RUNTIME_ERROR$'):
      self.run_protocol(b'SenseCAP> Guru Meditation Error\n', False, [0, 1])

  def test_unrecognized_output_is_not_a_success(self):
    with self.assertRaisesRegex(RuntimeError, '^FACTORY_RUNTIME_UNCONFIRMED$'):
      self.run_protocol(b'SYNTHETIC_PRIVATE_VALUE\n', False, [0, 1, 36])


if __name__ == '__main__':
  unittest.main()
