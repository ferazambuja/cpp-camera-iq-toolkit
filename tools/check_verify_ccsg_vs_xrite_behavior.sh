#!/usr/bin/env bash
set -euo pipefail

repo_root="$(git rev-parse --show-toplevel)"
verify_script="${VERIFY_SCRIPT:-${repo_root}/tools/verify_ccsg_vs_xrite.py}"
tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

ours_csv="${tmp_dir}/ours.csv"
xrite_after="${tmp_dir}/after.txt"
xrite_before="${tmp_dir}/before.txt"
layout_key="${tmp_dir}/ColorChecker SG by rows.txt"
missing_layout_key="${tmp_dir}/missing-layout.txt"

{
  printf 'patch_id'
  for wl in $(seq 380 10 730); do
    printf ',%s' "$wl"
  done
  printf '\n'
  for pid in A1 A2 A3 A4 A5 A6 N1 N2 N3 N4 N5 N6; do
    printf '%s' "$pid"
    for _ in $(seq 380 10 730); do
      printf ',1.0'
    done
    printf '\n'
  done
} > "$ours_csv"

write_xrite_fixture() {
  local path="$1"
  {
    printf 'BEGIN_DATA\n'
    for pid in A1 A2 A3 A4 A5 A6 N1 N2 N3 N4 N5 N6; do
      printf '%s\t100.0\t0.0\t0.0\n' "$pid"
    done
    printf 'END_DATA\n'
  } > "$path"
}

write_xrite_fixture "$xrite_after"
write_xrite_fixture "$xrite_before"

{
  letters=(A B C D E F G H I J K L M N)
  for number in $(seq 1 10); do
    col=0
    for letter in "${letters[@]}"; do
      printf '"%s%s" "synthetic" %d %d\n' "$letter" "$number" "$col" "$((number - 1))"
      col=$((col + 1))
    done
  done
} > "$layout_key"

missing_output="$tmp_dir/missing-output.txt"
set +e
python3 "$verify_script" \
  --ours "$ours_csv" \
  --xrite-after "$xrite_after" \
  --xrite-before "$xrite_before" \
  --layout-key "$missing_layout_key" \
  > "$missing_output" 2>&1
missing_status=$?
set -e

if [[ "$missing_status" -eq 0 ]]; then
  echo "expected missing layout key to fail, got exit 0" >&2
  cat "$missing_output" >&2
  exit 1
fi
if ! grep -q "layout key not found" "$missing_output"; then
  echo "expected missing layout key diagnostic" >&2
  cat "$missing_output" >&2
  exit 1
fi
if grep -q "labels are physically correct" "$missing_output"; then
  echo "missing-layout run must not print the physical-label conclusion" >&2
  cat "$missing_output" >&2
  exit 1
fi

present_output="$tmp_dir/present-output.txt"
python3 "$verify_script" \
  --ours "$ours_csv" \
  --xrite-after "$xrite_after" \
  --xrite-before "$xrite_before" \
  --layout-key "$layout_key" \
  > "$present_output" 2>&1

if ! grep -q "layout-key geometry proof: PASS" "$present_output"; then
  echo "expected layout-key proof to pass with synthetic layout" >&2
  cat "$present_output" >&2
  exit 1
fi
if ! grep -q "confirmed BOTH ways" "$present_output"; then
  echo "expected full two-proof conclusion when layout key is present" >&2
  cat "$present_output" >&2
  exit 1
fi
