#!/usr/bin/env bash
set -euo pipefail

test_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(cd "${test_dir}/.." && pwd)"

g++ -std=c++11 -Wall -Wextra -Werror \
  "${project_dir}/arduino/ATmega2560_Showroom/ChargerProtocol.cpp" \
  "${test_dir}/protocol_tests.cpp" \
  -o "${test_dir}/protocol_tests"

"${test_dir}/protocol_tests"
