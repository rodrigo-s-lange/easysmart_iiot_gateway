#!/usr/bin/env bash

# Defaults for local Zephyr development on this server.
export ZEPHYR_BASE="${ZEPHYR_BASE:-/home/rodrigo/zephyrproject/zephyr}"
export ZEPHYR_SDK_INSTALL_DIR="${ZEPHYR_SDK_INSTALL_DIR:-/home/rodrigo/zephyr-sdk-0.17.3}"
export CMAKE_BUILD_PARALLEL_LEVEL="${CMAKE_BUILD_PARALLEL_LEVEL:-1}"

if [ -f /home/rodrigo/zephyrproject/.venv/bin/activate ]; then
  # shellcheck disable=SC1091
  . /home/rodrigo/zephyrproject/.venv/bin/activate
fi

ulimit -s unlimited 2>/dev/null || true
