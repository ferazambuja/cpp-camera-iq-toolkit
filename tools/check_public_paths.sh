#!/usr/bin/env bash
set -euo pipefail

repo_root="$(git rev-parse --show-toplevel)"
cd "$repo_root"

home_prefix="/""Users/"
volume_prefix="/""Volumes/"
private_path_pattern="(${home_prefix}|${volume_prefix})"

if git grep -n -E "$private_path_pattern" -- \
  ':!configs/datasets.local.json' \
  ':!data/private/**'
then
  echo "private absolute path found in tracked public files" >&2
  exit 1
fi
