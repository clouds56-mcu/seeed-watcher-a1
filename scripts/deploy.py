"""Local deployment: project fixed status codes only; never forward device output."""
import argparse
import os
import re
from pathlib import Path
import struct
import subprocess
import sys
import time
import zlib


def run_tool(port, *args):
  command = [sys.executable, '-m', 'esptool', '--chip', 'esp32s3',
             '--port', port, '--baud', '460800', '--after', 'no_reset', *args]
  result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
  if result.returncode:
    raise RuntimeError('SERIAL_OPERATION_FAILED')


def deploy(port):
  os.umask(0o077)
  local = Path('.local') / time.strftime('backup-%Y%m%d-%H%M%S')
  local.mkdir(parents=True)
  table_path = local / 'partition-table.bin'
  run_tool(port, 'read_flash', '0x8000', '0x1000', str(table_path))
  entries = {}
  table = table_path.read_bytes()
  for offset in range(0, 0xc00, 32):
    magic, kind, subtype, start, size, name, flags = struct.unpack_from('<HBBII16sI', table, offset)
    if magic != 0x50aa:
      break
    entries[name.rstrip(b'\0')] = (kind, subtype, start, size, flags)
  expected = {
    b'nvsfactory': (1, 2, 0x9000, 200 * 1024, 0),
    b'otadata': (1, 0, 0x10d000, 0x2000, 0),
    b'ota_0': (0, 0x10, 0x110000, 12 * 1024 * 1024, 0),
    b'ota_1': (0, 0x11, 0xd10000, 12 * 1024 * 1024, 0),
  }
  if any(entries.get(key) != value for key, value in expected.items()):
    raise RuntimeError('PARTITION_LAYOUT_UNSUPPORTED')
  image = Path('build/watcher_sound_meter.bin')
  if not image.is_file() or not 0 < image.stat().st_size <= expected[b'ota_0'][3]:
    raise RuntimeError('APPLICATION_IMAGE_INVALID')
  print('PARTITION_LAYOUT_VERIFIED', flush=True)
  run_tool(port, 'read_flash', '0x10d000', '0x2000', str(local / 'otadata.bin'))
  print('APPLICATION_BACKUP_STARTED', flush=True)
  backup_size = (image.stat().st_size + 4095) // 4096 * 4096
  run_tool(port, 'read_flash', '0x110000', str(backup_size), str(local / 'ota_0-prefix.bin'))
  print('APPLICATION_BACKUP_COMPLETE', flush=True)
  # Select OTA slot 0 in VALID state, keeping the existing bootloader and table.
  sequence = struct.pack('<I', 1)
  entry = sequence + b'\xff' * 20 + struct.pack('<II', 2, zlib.crc32(sequence, 0xffffffff))
  selection = local / 'demo-otadata.bin'
  selection.write_bytes(entry + b'\xff' * (8192 - len(entry)))
  run_tool(port, 'write_flash', '0x110000', str(image))
  print('APPLICATION_FLASH_VERIFIED', flush=True)
  run_tool(port, 'write_flash', '0x10d000', str(selection))
  print('BOOT_SELECTION_VERIFIED', flush=True)
  # Only ROM/loader traffic is handled above. No factory application logs are exported.
  import serial
  with serial.Serial(port, 115200, timeout=0.2) as device:
    device.dtr = False
    device.rts = True
    time.sleep(0.1)
    device.rts = False
    deadline = time.monotonic() + 25
    buffer = b''
    ready = running = signal = rendered = timing = controls = failed = False
    perf_reports = 0
    perf_pattern = re.compile(rb'PERF_V5 elapsed_ms=([0-9]+) frames=([0-9]+) fft_avg_us=([0-9]+) fft_max_us=([0-9]+) ui_wait_avg_us=([0-9]+) ui_wait_max_us=([0-9]+) render_avg_ms=([0-9]+) render_max_ms=([0-9]+) pixels_avg=([0-9]+)')
    while time.monotonic() < deadline:
      buffer = (buffer + device.read(1024))[-4096:]
      while b'\n' in buffer:
        line, buffer = buffer.split(b'\n', 1)
        line = line.rstrip(b'\r')
        ready |= line == b'SOUND_METER_READY_V5'
        running |= line == b'SOUND_METER_RUNNING_V5'
        signal |= line == b'SOUND_METER_SIGNAL_V5'
        rendered |= line == b'SPECTRUM_RENDERED_V5'
        timing |= line == b'SPECTRUM_TIMING_OK_V5'
        controls |= line == b'SPECTRUM_CONTROLS_READY_V5'
        failed |= line == b'SOUND_METER_MIC_ERROR'
        match = perf_pattern.fullmatch(line)
        if match:
          print('PERF_COUNTS ' + ' '.join(str(int(value)) for value in match.groups()), flush=True)
          perf_reports += 1
      if failed:
        raise RuntimeError('DEMO_MIC_ERROR')
      if ready and running and signal and rendered and timing and controls and perf_reports >= 2:
        print('DEMO_SPECTRUM_CONTROLS_SIGNAL_TIMING_VERIFIED', flush=True)
        return
  raise RuntimeError('DEMO_RUNTIME_UNCONFIRMED')


if __name__ == '__main__':
  parser = argparse.ArgumentParser()
  parser.add_argument('--port', required=True)
  arguments = parser.parse_args()
  try:
    deploy(arguments.port)
  except Exception as error:
    allowed = {'SERIAL_OPERATION_FAILED', 'PARTITION_LAYOUT_UNSUPPORTED',
               'APPLICATION_IMAGE_INVALID', 'DEMO_MIC_ERROR', 'DEMO_RUNTIME_UNCONFIRMED'}
    print(str(error) if type(error) is RuntimeError and str(error) in allowed else 'DEPLOYMENT_FAILED')
    sys.exit(1)
