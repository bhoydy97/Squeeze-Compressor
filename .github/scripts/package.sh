#!/usr/bin/env bash
# Zips every built plugin for this system, plus one bundle zip with the plugins listed in
# packaging/bundle-plugins.txt (what the bundle on the website sells):
#   dist/<Plugin>-<label>-<System>.zip  and  dist/LBAudio-Bundle-<label>-<System>.zip
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
mkdir -p "$DIST" "$STAGE/bundle"

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

in_bundle() { grep -qx "$1" <(sed 's/#.*//; s/[[:space:]]//g' "$ROOT/packaging/bundle-plugins.txt"); }

echo "Packaging $LABEL for $SYSTEM:"
count=0
for dir in "$ROOT"/plugins/*/; do
  name="$(basename "$dir")"
  artefacts="$ROOT/build/plugins/$name/${name}_artefacts/Release"
  [[ -d "$artefacts/VST3/$name.vst3" ]] || { echo "::error::$name.vst3 was not built"; exit 1; }

  mkdir -p "$STAGE/$name"
  targets=("$STAGE/$name")
  if in_bundle "$name"; then targets+=("$STAGE/bundle"); fi
  for target in "${targets[@]}"; do
    copy_bundle "$artefacts/VST3/$name.vst3" "$target"
    if [[ "$SYSTEM" == macOS ]]; then copy_bundle "$artefacts/AU/$name.component" "$target"; fi
  done
  add_docs "$STAGE/$name"
  make_zip "$STAGE/$name" "$DIST/$name-$LABEL-$SUFFIX.zip"
  count=$((count + 1))
done

add_docs "$STAGE/bundle"
make_zip "$STAGE/bundle" "$DIST/LBAudio-Bundle-$LABEL-$SUFFIX.zip"
echo "$count plugins + bundle ($(ls "$STAGE/bundle" | grep -c -E "\.(vst3)$") in the bundle)."
