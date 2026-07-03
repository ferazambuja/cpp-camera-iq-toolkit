#!/usr/bin/env bash
set -euo pipefail

repo_root="$(git rev-parse --show-toplevel)"
cd "$repo_root"

max_sample_bytes=$((1024 * 1024))
max_raw_placeholder_bytes=$((4 * 1024))

fail=0
while IFS= read -r -d '' path; do
  size="$(wc -c < "$path" | tr -d ' ')"
  if (( size > max_sample_bytes )); then
    echo "sample fixture too large (${size} bytes): ${path}" >&2
    fail=1
  fi

  case "$path" in
    *.RAF|*.raf|*.NEF|*.nef|*.ARW|*.arw|*.CR2|*.cr2|*.CR3|*.cr3|*.IIQ|*.iiq|*.dng|*.DNG|*.tif|*.TIF|*.tiff|*.TIFF)
      if (( size > max_raw_placeholder_bytes )); then
        echo "RAW-like sample fixture too large (${size} bytes): ${path}" >&2
        fail=1
      fi
      if ! grep -qi 'synthetic public fixture\|not a real' "$path"; then
        echo "RAW-like sample fixture lacks placeholder marker: ${path}" >&2
        fail=1
      fi
      ;;
  esac
# Scan tracked files only: what is committed is what ships publicly, and this
# avoids false positives from untracked local RAWs a dev may drop under
# data/samples/. In CI (fresh clone) this equals the on-disk set.
done < <(git ls-files -z -- data/samples)

exit "$fail"
