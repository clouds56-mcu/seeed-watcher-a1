#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
python3 scripts/prepare_factory.py
cd factory
exec ../scripts/build.sh
