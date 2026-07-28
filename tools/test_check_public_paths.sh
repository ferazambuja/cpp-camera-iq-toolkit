#!/usr/bin/env bash
set -euo pipefail

checker="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"

expect_rejected() {
  case_name="$1"
  relative_path="$2"
  content="$3"
  fixture_root="$(mktemp -d)"
  git -C "$fixture_root" init -q
  git -C "$fixture_root" config user.email fixture@example.invalid
  git -C "$fixture_root" config user.name Fixture
  mkdir -p "$fixture_root/$(dirname "$relative_path")"
  printf '%s\n' "$content" > "$fixture_root/$relative_path"
  git -C "$fixture_root" add -f "$relative_path"
  if (cd "$fixture_root" && bash "$checker"); then
    rm -rf "$fixture_root"
    echo "privacy checker accepted $case_name" >&2
    exit 1
  fi
  rm -rf "$fixture_root"
}

expect_rejected "tracked local config" \
  "configs/datasets.local.json" '{"root":"relative"}'
expect_rejected "tracked private data" \
  "data/private/secret.txt" "private placeholder"
backticked_windows_path='Do not publish `C:'"\\private\\capture.RAF"'`.'
parenthesized_windows_path='Do not publish (D:'"/private/capture.RAF"').'
expect_rejected "backticked Windows path" \
  "README.md" "$backticked_windows_path"
expect_rejected "parenthesized Windows path" \
  "README.md" "$parenthesized_windows_path"

printf 'privacy checker rejects each forbidden location and path form\n'
