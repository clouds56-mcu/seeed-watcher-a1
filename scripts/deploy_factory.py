"""Deploy the integrated application; project only fixed runtime status codes."""
import argparse
import json
from pathlib import Path
import sys
import time

from deploy import flash_application


ERROR_CODES = {
  'SERIAL_OPERATION_FAILED', 'PARTITION_LAYOUT_UNSUPPORTED',
  'APPLICATION_IMAGE_INVALID', 'FACTORY_RUNTIME_UNCONFIRMED',
  'FACTORY_RUNTIME_ERROR', 'FACTORY_OFFLINE_BOUNDARY_FAILED',
  'FACTORY_TEST_CONFIGURATION_MISMATCH',
}


def deploy(port, self_test=False):
  configuration = json.loads(Path('factory/build/config/sdkconfig.json').read_text())
  if bool(configuration.get('WATCHER_NATIVE_SELF_TEST')) != self_test:
    raise RuntimeError('FACTORY_TEST_CONFIGURATION_MISMATCH')
  flash_application(port, Path('factory/build/factory_spectrum.bin'))
  import serial
  expected = {
    b'NATIVE_FACTORY_OFFLINE_READY_V1': 1,
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
    deadline = time.monotonic() + (45 if self_test else 15)
    buffer = b''
    while time.monotonic() < deadline:
      buffer = (buffer + device.read(1024))[-4096:]
      while b'\n' in buffer:
        line, buffer = buffer.split(b'\n', 1)
        line = line.rstrip(b'\r')
        if line == b'NATIVE_FACTORY_SERVICES_ENABLED_V1':
          raise RuntimeError('FACTORY_OFFLINE_BOUNDARY_FAILED')
        if any(marker in line for marker in (b'Guru Meditation', b'assert failed', b'abort() was called')):
          raise RuntimeError('FACTORY_RUNTIME_ERROR')
        if line in (b'NATIVE_SPECTRUM_AUDIO_ERROR_V1', b'NATIVE_SPECTRUM_BUSY_V1'):
          raise RuntimeError('FACTORY_RUNTIME_ERROR')
        if line in counts:
          counts[line] += 1
          print(line.decode('ascii'), flush=True)
      if self_test and all(counts[key] >= value for key, value in expected.items()):
        print('FACTORY_OFFLINE_LIFECYCLE_VERIFIED', flush=True)
        return
    if not self_test and counts[b'NATIVE_FACTORY_OFFLINE_READY_V1'] == 1:
      print('FACTORY_OFFLINE_BOOT_VERIFIED', flush=True)
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
