#!/usr/bin/env bash
# Signs every plugin's macOS VST3 and AU with a Developer ID certificate, then has
# Apple notarize them so Gatekeeper lets them load without warnings.
#
# Needs these GitHub repository secrets:
#   MACOS_CERTIFICATE           base64 of the "Developer ID Application" .p12 export
#   MACOS_CERTIFICATE_PASSWORD  password chosen when exporting the .p12
#   APPLE_ID                    Apple Account email used for the developer program
#   APPLE_APP_PASSWORD          app-specific password from account.apple.com
#   APPLE_TEAM_ID               10-character Team ID from developer.apple.com/account
set -euo pipefail

BUNDLES=()
for bundle in build/plugins/*/*_artefacts/Release/VST3/*.vst3 build/plugins/*/*_artefacts/Release/AU/*.component; do
  [[ -e "$bundle" ]] && BUNDLES+=("$bundle")
done
if [[ ${#BUNDLES[@]} -eq 0 ]]; then
  echo "::error::No plugin bundles found under build/plugins"
  exit 1
fi
echo "Bundles to sign: ${#BUNDLES[@]}"

for name in MACOS_CERTIFICATE MACOS_CERTIFICATE_PASSWORD APPLE_ID APPLE_APP_PASSWORD APPLE_TEAM_ID; do
  if [[ -z "${!name:-}" ]]; then
    echo "::error::Missing GitHub secret $name. See the launch guide, Mac signing section."
    exit 1
  fi
done

# 1) Put the certificate in a temporary keychain
KEYCHAIN="$RUNNER_TEMP/signing.keychain-db"
KEYCHAIN_PASSWORD="$(uuidgen)"
echo "$MACOS_CERTIFICATE" | base64 --decode > "$RUNNER_TEMP/certificate.p12"
security create-keychain -p "$KEYCHAIN_PASSWORD" "$KEYCHAIN"
security set-keychain-settings -lut 21600 "$KEYCHAIN"
security unlock-keychain -p "$KEYCHAIN_PASSWORD" "$KEYCHAIN"
security import "$RUNNER_TEMP/certificate.p12" -k "$KEYCHAIN" -P "$MACOS_CERTIFICATE_PASSWORD" \
  -f pkcs12 -A -T /usr/bin/codesign -T /usr/bin/security
security set-key-partition-list -S apple-tool:,apple:,codesign: -s -k "$KEYCHAIN_PASSWORD" "$KEYCHAIN" > /dev/null
# shellcheck disable=SC2046
security list-keychains -d user -s "$KEYCHAIN" $(security list-keychains -d user | tr -d '"')
rm -f "$RUNNER_TEMP/certificate.p12"

IDENTITY="$(security find-identity -v -p codesigning "$KEYCHAIN" | grep 'Developer ID Application' | head -1 | sed -E 's/.*"(.+)"/\1/')"
if [[ -z "$IDENTITY" ]]; then
  echo "::error::No 'Developer ID Application' certificate found in MACOS_CERTIFICATE."
  security find-identity -v "$KEYCHAIN"
  exit 1
fi
echo "Signing with: $IDENTITY"

# 2) Sign each plugin bundle (hardened runtime + secure timestamp, as notarization requires)
for bundle in "${BUNDLES[@]}"; do
  codesign --force --options runtime --timestamp --keychain "$KEYCHAIN" --sign "$IDENTITY" "$bundle"
  codesign --verify --strict --verbose=2 "$bundle"
done

# 3) Send them all to Apple's notary service in one go and wait for the result
STAGE="$RUNNER_TEMP/notarize"
mkdir -p "$STAGE"
cp -R "${BUNDLES[@]}" "$STAGE/"
ditto -c -k "$STAGE" "$RUNNER_TEMP/notarize.zip"

RESULT="$(xcrun notarytool submit "$RUNNER_TEMP/notarize.zip" \
  --apple-id "$APPLE_ID" --password "$APPLE_APP_PASSWORD" --team-id "$APPLE_TEAM_ID" \
  --wait --output-format json)"
echo "$RESULT"
STATUS="$(echo "$RESULT" | /usr/bin/python3 -c 'import json,sys; print(json.load(sys.stdin).get("status",""))')"
if [[ "$STATUS" != "Accepted" ]]; then
  SUBMISSION="$(echo "$RESULT" | /usr/bin/python3 -c 'import json,sys; print(json.load(sys.stdin).get("id",""))')"
  echo "::error::Notarization status: $STATUS"
  if [[ -n "$SUBMISSION" ]]; then
    xcrun notarytool log "$SUBMISSION" --apple-id "$APPLE_ID" --password "$APPLE_APP_PASSWORD" --team-id "$APPLE_TEAM_ID" || true
  fi
  exit 1
fi

# 4) Attach the notarization ticket so the plugins also pass checks offline
for bundle in "${BUNDLES[@]}"; do
  xcrun stapler staple "$bundle"
  xcrun stapler validate "$bundle"
done

security delete-keychain "$KEYCHAIN" || true
echo "Signed and notarized."
