"""Deploy the integrated application; project only fixed runtime status codes."""
import argparse
import json
import re
from pathlib import Path
import sys
import time

from deploy import flash_application


ERROR_CODES = {
  'SERIAL_OPERATION_FAILED', 'PARTITION_LAYOUT_UNSUPPORTED',
  'APPLICATION_IMAGE_INVALID', 'FACTORY_RUNTIME_UNCONFIRMED',
  'FACTORY_RUNTIME_ERROR',
  'FACTORY_TEST_CONFIGURATION_MISMATCH',
}


def deploy(port, self_test=False):
  configuration = json.loads(Path('factory/build/config/sdkconfig.json').read_text())
  if bool(configuration.get('WATCHER_NATIVE_SELF_TEST')) != self_test:
    raise RuntimeError('FACTORY_TEST_CONFIGURATION_MISMATCH')
  flash_application(port, Path('factory/build/factory_spectrum.bin'))
  verify_runtime(port, self_test)


def verify_runtime(port, self_test=False):
  import serial
  expected = {
    b'NATIVE_FACTORY_MENU_READY_V2': 1,
  }
  if self_test:
    expected.update({
      b'NATIVE_SPECTRUM_ENTERED_V1': 2,
      b'NATIVE_SPECTRUM_RUNNING_V1': 2,
      b'NATIVE_SPECTRUM_EXITED_V1': 2,
      b'NATIVE_LIFECYCLE_SELF_TEST_PASS_V1': 1,
    })
  counts = dict.fromkeys(expected, 0)
  with serial.Serial(port, 115200, timeout=0.2) as device:
    device.dtr = False
    device.rts = True
    time.sleep(0.1)
    device.rts = False
    deadline = time.monotonic() + (75 if self_test else 35)
    buffer = b''
    while time.monotonic() < deadline:
      buffer = (buffer + device.read(1024))[-4096:]
      while b'\n' in buffer:
        line, buffer = buffer.split(b'\n', 1)
        # The stock console can print a fixed prompt without a newline.
        line = re.sub(rb'\x1b\[[0-?]*[ -/]*[@-~]', b'', line).strip()
        line = line.removeprefix(b'SenseCAP>').strip()
        if any(marker in line for marker in (b'Guru Meditation', b'assert failed', b'abort() was called', b'Task watchdog got triggered')):
          raise RuntimeError('FACTORY_RUNTIME_ERROR')
        if line in (b'NATIVE_SPECTRUM_AUDIO_ERROR_V1', b'NATIVE_SPECTRUM_BUSY_V1'):
          raise RuntimeError('FACTORY_RUNTIME_ERROR')
        memory = re.fullmatch(rb'NATIVE_HEAP_BEFORE_BLE_V2 free=([0-9]+) largest=([0-9]+)', line)
        if memory:
          print('FACTORY_RADIO_HEAP_COUNTS ' + ' '.join(str(int(value)) for value in memory.groups()), flush=True)
        if line in (b'NATIVE_FACTORY_BOARD_READY_V2', b'NATIVE_FACTORY_UI_READY_V2', b'NATIVE_FACTORY_SERVICES_READY_V2'):
          print(line.decode('ascii'), flush=True)
        if line in counts:
          counts[line] += 1
          print(line.decode('ascii'), flush=True)
      if self_test and all(counts[key] >= value for key, value in expected.items()):
        print('FACTORY_MENU_LIFECYCLE_VERIFIED', flush=True)
        return
    if not self_test and counts[b'NATIVE_FACTORY_MENU_READY_V2'] == 1:
      print('FACTORY_MENU_BOOT_VERIFIED', flush=True)
      return
  raise RuntimeError('FACTORY_RUNTIME_UNCONFIRMED')


if __name__ == '__main__':
  parser = argparse.ArgumentParser()
  parser.add_argument('--port', required=True)
  parser.add_argument('--self-test', action='store_true')
  arguments = parser.parse_args()
  try:
    deploy(arguments.port, arguments.self_test)
  except Exception as error:
    code = str(error)
    print(code if type(error) is RuntimeError and code in ERROR_CODES else 'FACTORY_DEPLOYMENT_FAILED')
    sys.exit(1)
