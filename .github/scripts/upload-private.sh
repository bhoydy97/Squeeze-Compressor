#!/usr/bin/env bash
# Upload files to the private draft release $DRAFT_ID, replacing any with the same name.
#   upload-private.sh <file>...    (needs GH_TOKEN, GITHUB_REPOSITORY, DRAFT_ID)
set -euo pipefail
[[ -n "${DRAFT_ID:-}" ]] || { echo "::error::No draft release to upload to"; exit 1; }
for f in "$@"; do
  name="$(basename "$f")"
  old=$(gh api "repos/$GITHUB_REPOSITORY/releases/$DRAFT_ID/assets?per_page=100" --jq ".[] | select(.name == \"$name\") | .id")
  for id in $old; do gh api -X DELETE "repos/$GITHUB_REPOSITORY/releases/assets/$id" > /dev/null; done
  curl -sSf -X POST -H "Authorization: Bearer $GH_TOKEN" -H "Content-Type: application/zip" \
       --data-binary @"$f" "https://uploads.github.com/repos/$GITHUB_REPOSITORY/releases/$DRAFT_ID/assets?name=$name" > /dev/null
  echo "  uploaded $name"
done
