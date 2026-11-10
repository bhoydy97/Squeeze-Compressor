#!/usr/bin/env bash
# Zips every built plugin for this system, plus one zip per bundle in packaging/bundles/ (each file
# there lists the plugins in that bundle, as the website sells it):
#   dist/<Plugin>-<label>-<System>.zip  and  dist/LBAudio-<Bundle>-Bundle-<label>-<System>.zip
# Each zip holds the plugin(s), INSTALL.txt for the system and THIRD_PARTY_NOTICES.txt.
#   package.sh <label>     e.g. package.sh v1.1.0
set -euo pipefail

LABEL="${1:?usage: package.sh <label>}"
SYSTEM="${RUNNER_OS:?RUNNER_OS not set}" # Windows, macOS or Linux (set by GitHub Actions)
ROOT="$(pwd)"
DIST="$ROOT/dist"
TEMP_DIR="${RUNNER_TEMP:-/tmp}"
if command -v cygpath > /dev/null; then TEMP_DIR="$(cygpath -u "$TEMP_DIR")"; fi # Windows (Git Bash)
STAGE="$TEMP_DIR/lbaudio-package"
case "$SYSTEM" in
  macOS) SUFFIX="macOS-VST3-AU" ;;
  *)     SUFFIX="$SYSTEM-VST3" ;;
esac

rm -rf "$STAGE"
mkdir -p "$DIST"

copy_bundle() { # <source bundle> <destination folder>
  if [[ "$SYSTEM" == macOS ]]; then ditto "$1" "$2/$(basename "$1")"; else cp -R "$1" "$2/"; fi
}

make_zip() { # <folder whose contents go in the zip> <zip file>
  rm -f "$2"
  case "$SYSTEM" in
    macOS)   ditto -c -k "$1" "$2" ;;
    Windows) (cd "$1" && 7z a -tzip -r "$2" . > /dev/null) ;;
    *)       (cd "$1" && zip -r -y -q "$2" .) ;;
  esac
  echo "  $(basename "$2")"
}

add_docs() { # <folder>
  cp "$ROOT/build/THIRD_PARTY_NOTICES.txt" "$1/"
  cp "$ROOT/packaging/INSTALL-$SYSTEM.txt" "$1/INSTALL.txt"
}

bundle_list() { sed 's/#.*//; s/[[:space:]]//g' "$1" | grep -v '^$'; }

echo "Packaging $LABEL for $SYSTEM:"
count=0
for name in $(cat "$ROOT/build/plugins.txt"); do   # the plugins in this build (one still in the works isn't listed)
  artefacts="$ROOT/build/plugins/$name/${name}_artefacts/Release"
  [[ -d "$artefacts/VST3/$name.vst3" ]] || { echo "::error::$name.vst3 was not built"; exit 1; }

  mkdir -p "$STAGE/$name"
  copy_bundle "$artefacts/VST3/$name.vst3" "$STAGE/$name"
  if [[ "$SYSTEM" == macOS ]]; then copy_bundle "$artefacts/AU/$name.component" "$STAGE/$name"; fi
  add_docs "$STAGE/$name"
  make_zip "$STAGE/$name" "$DIST/$name-$LABEL-$SUFFIX.zip"
  count=$((count + 1))
done

bundles=0
for list in "$ROOT"/packaging/bundles/*.txt; do
  bundle="$(basename "$list" .txt)"; dir="$STAGE/bundle-$bundle"; mkdir -p "$dir"; n=0
  while read -r name; do
    [[ -d "$STAGE/$name" ]] || { echo "::error::$bundle lists $name, which isn't a plugin"; exit 1; }
    for item in "$STAGE/$name"/*.vst3 "$STAGE/$name"/*.component; do [[ -e "$item" ]] && copy_bundle "$item" "$dir"; done
    n=$((n + 1))
  done < <(bundle_list "$list")
  add_docs "$dir"
  make_zip "$dir" "$DIST/LBAudio-$bundle-Bundle-$LABEL-$SUFFIX.zip"
  bundles=$((bundles + 1))
done
echo "$count plugins + $bundles bundles."
