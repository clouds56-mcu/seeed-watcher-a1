"""Materialize pinned public factory sources without modifying user data."""
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SDK = ROOT / '.deps' / 'watcher-sdk'


def git(*args):
  result = subprocess.run(['git', *map(str, args)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
  if result.returncode:
    raise RuntimeError('FACTORY_SOURCE_OPERATION_FAILED')
  return result.stdout


def prepare():
  specification = json.loads((ROOT / 'factory/upstream.json').read_text())
  if not SDK.exists():
    SDK.parent.mkdir(exist_ok=True)
    git('clone', '--no-checkout', '--filter=blob:none', specification['repository_url'], SDK)
    git('-C', SDK, 'fetch', '--depth', '1', 'origin', specification['commit'])
    git('-C', SDK, 'checkout', '--detach', specification['commit'])
  if git('-C', SDK, 'rev-parse', 'HEAD').decode().strip() != specification['commit']:
    raise RuntimeError('FACTORY_SOURCE_REVISION_MISMATCH')
  patch = ROOT / 'factory/upstream.patch'
  current = git('-C', SDK, 'diff', '--binary', '--', 'examples/factory_firmware/main')
  if current and current != patch.read_bytes():
    raise RuntimeError('FACTORY_SOURCE_MODIFICATIONS_UNEXPECTED')
  if not current:
    git('-C', SDK, 'apply', '--check', patch)
    git('-C', SDK, 'apply', patch)
  print('FACTORY_SOURCE_READY')


if __name__ == '__main__':
  prepare()
