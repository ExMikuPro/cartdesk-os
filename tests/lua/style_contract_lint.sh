#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)

for pattern in \
  'self\.(elapsed|ticks|count|fixed_count|fixed_dt|level|duty|direction|accumulator|button|slider|file|log_file)' \
  'ui\.button\.(create|draw|get_screen)' \
  'ui\.slider\.(create|draw|get_screen)' \
  ':(set_text|set_pos|set_size|set_value|delete)\(' \
  'gpio\.(pinMode|digitalRead|digitalWrite)' \
  '(^|[^[:alnum:]_])(pinMode|digitalRead|digitalWrite)([^[:alnum:]_]|$)' \
  'pwm\.(setFreq|getFreq)'
do
  if grep -REn "$pattern" "$ROOT_DIR/examples/lua" >/dev/null; then
    echo "style_contract_lint: forbidden Lua example pattern: $pattern" >&2
    grep -REn "$pattern" "$ROOT_DIR/examples/lua" >&2
    exit 1
  fi
done

echo "style_contract_lint: ok"
