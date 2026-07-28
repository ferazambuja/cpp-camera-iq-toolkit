#!/usr/bin/env bash
set -euo pipefail

repo_root="$(git rev-parse --show-toplevel)"
cd "$repo_root"

mac_home_prefix="/""Users/"
volume_prefix="/""Volumes/"
linux_home_prefix="/""home/"
tmp_prefix="/""tmp/"
private_var_prefix="/""private/var/"
file_uri_prefix="file""://"
windows_drive_pattern="(^|[[:space:][:punct:]])[A-Za-z]:([/]|\\\\)"
private_path_pattern="(${mac_home_prefix}|${volume_prefix}|${linux_home_prefix}|${tmp_prefix}|${private_var_prefix}|${file_uri_prefix}|${windows_drive_pattern})"

if git ls-files -- configs/datasets.local.json data/private | grep -q .; then
  echo "forbidden private location is tracked:" >&2
  git ls-files -- configs/datasets.local.json data/private >&2
  exit 1
fi

if git grep -n -E "$private_path_pattern"
then
  echo "private absolute path found in tracked public files" >&2
  exit 1
fi
