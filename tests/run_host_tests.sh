#!/usr/bin/env bash
set -euo pipefail

test_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(cd "${test_dir}/.." && pwd)"
test_binary="$(mktemp "${TMPDIR:-/tmp}/charger-protocol-tests.XXXXXX")"
trap 'rm -f "${test_binary}"' EXIT

g++ -std=c++11 -Wall -Wextra -Werror \
  "${project_dir}/arduino/ATmega2560_Showroom/ChargerProtocol.cpp" \
  "${test_dir}/protocol_tests.cpp" \
  -o "${test_binary}"

"${test_binary}"
python3 "${test_dir}/showroom_contract_tests.py"
