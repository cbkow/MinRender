#!/usr/bin/env bash
# Build, sign, and notarize a distributable macOS release.
#
# Output:
#   release/MinRender-<version>.app   — signed & stapled .app
#   release/MinRender-<version>.dmg   — signed, notarized & stapled DMG
#
# Notarization uses the keychain profile named in $NOTARY_PROFILE (default
# AC_PASSWORD, matching the convention used by the QCView-Player release
# script). Configure once with:
#
#   xcrun notarytool store-credentials AC_PASSWORD \
#       --apple-id <your-developer-apple-id> \
#       --team-id 5Z4S9VHV56 \
#       --password <app-specific-password-from-appleid.apple.com>
#
# If the profile is missing, the script still produces a signed .app +
# DMG, just unstapled — they'll work on YOUR Mac (right-click Open the
# first time on a downloaded one) but Gatekeeper warns on other Macs.

set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO"

# ---------------------------------------------------------------------------
# Config — these are the only knobs you should normally touch.
# ---------------------------------------------------------------------------
QT_PREFIX="${QT_PREFIX:-/Users/chris/Qt/6.11.0/macos}"
SIGN_ID="${SIGN_ID:-Developer ID Application: Christopher Bialkowski (5Z4S9VHV56)}"
TEAM_ID="${TEAM_ID:-5Z4S9VHV56}"
NOTARY_PROFILE="${NOTARY_PROFILE:-AC_PASSWORD}"
BUILD_DIR="${BUILD_DIR:-build-mac-release}"
RELEASE_DIR="${RELEASE_DIR:-release}"
ENTITLEMENTS="${ENTITLEMENTS:-$REPO/scripts/entitlements.plist}"

# Version comes from CMakeLists project() line so we can't drift.
VERSION="$(grep -E '^project\(MinRender VERSION' CMakeLists.txt \
            | sed -E 's/.*VERSION ([0-9.]+).*/\1/')"
APP_NAME="MinRender"
APP="$BUILD_DIR/minrender.app"
DMG="$RELEASE_DIR/${APP_NAME}-${VERSION}.dmg"

echo "==> Release: ${APP_NAME} ${VERSION}"
echo "    sign-id:        $SIGN_ID"
echo "    team:           $TEAM_ID"
echo "    notary profile: $NOTARY_PROFILE"
echo "    qt prefix:      $QT_PREFIX"
echo "    build dir:      $BUILD_DIR"
echo "    release dir:    $RELEASE_DIR"
echo

# ---------------------------------------------------------------------------
# Sanity check: we need the cert before we burn ten minutes building.
# ---------------------------------------------------------------------------
security find-identity -v -p codesigning | grep -q "$TEAM_ID" || {
    echo "ERROR: No signing identity matching team $TEAM_ID found in keychain."
    echo "       Run: security find-identity -v -p codesigning"
    exit 1
}

# ---------------------------------------------------------------------------
# 1. Build mr-agent (Rust release).
# ---------------------------------------------------------------------------
echo "==> [1/8] Building mr-agent (release)"
( cd mr-agent && cargo build --release )

# ---------------------------------------------------------------------------
# 2. Configure + build the Qt UI as a Release .app bundle.
# ---------------------------------------------------------------------------
echo "==> [2/8] Configuring (Release)"
cmake -S . -B "$BUILD_DIR" \
    -DCMAKE_PREFIX_PATH="$QT_PREFIX" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 \
    > /dev/null

echo "==> [3/8] Building minrender.app"
cmake --build "$BUILD_DIR" --target minrender -j8

# ---------------------------------------------------------------------------
# 3. macdeployqt — bundles every Qt framework the app uses into
#    Contents/Frameworks, rewrites @rpath references, and copies the QML
#    modules referenced by -qmldir. Without this the .app only runs on
#    Macs with Qt 6.11 installed at QT_PREFIX.
# ---------------------------------------------------------------------------
echo "==> [4/8] macdeployqt (bundling Qt frameworks)"
"$QT_PREFIX/bin/macdeployqt" "$APP" \
    -qmldir="$REPO/src/ui/qml" \
    || true   # macdeployqt errors loudly on QtSql plugins it can't fully resolve;
              # we delete that whole subtree below since we don't use QSqlDatabase
              # (SQLiteCpp links against the bundled sqlite3 amalgamation directly).

# Prune Qt plugin trees we don't use. macdeployqt copies them all by
# default and tries to scan their dylib deps — when those deps aren't
# present on the build host (Postgres.app, libiodbc, mimerapi) the
# scan errors but the plugins are still copied. Drop them now so they
# don't end up in the release.
rm -rf "$APP/Contents/PlugIns/sqldrivers"
rm -rf "$APP/Contents/PlugIns/printsupport"
rm -rf "$APP/Contents/PlugIns/networkinformation"

# ---------------------------------------------------------------------------
# 4. Strip any leftover signatures, then sign inside-out.
#    Per Apple's modern guidance (and what the QCView-Player script
#    does): sign deepest items first, work outward. --deep is deprecated
#    for new code; explicit per-binary signing avoids edge cases.
# ---------------------------------------------------------------------------
echo "==> [5/8] Cleaning extended attributes + stripping signatures"
# Codesign refuses files with com.apple.quarantine, com.apple.FinderInfo,
# or similar xattrs ("resource fork, Finder information, or similar
# detritus not allowed"). Source files copied through Finder or via curl
# can pick these up; clean the entire tree before signing.
xattr -cr "$APP"

codesign --remove-signature "$APP" 2>/dev/null || true
find "$APP/Contents/Frameworks" -type f \( -name '*.dylib' -o -perm -u+x \) \
    -exec codesign --remove-signature {} \; 2>/dev/null || true
find "$APP/Contents/PlugIns" -type f -name '*.dylib' \
    -exec codesign --remove-signature {} \; 2>/dev/null || true
codesign --remove-signature "$APP/Contents/MacOS/mr-agent" 2>/dev/null || true

echo "==> [6/8] Signing (inside-out)"
# 6a. Qt frameworks: each Versions/A/QtXxx Mach-O gets signed.
#     The .framework directory is then signed by the deep walk implied
#     by signing the .app shell at the end.
while IFS= read -r -d '' fw_binary; do
    codesign --force --sign "$SIGN_ID" --timestamp --options runtime \
        "$fw_binary"
done < <(find "$APP/Contents/Frameworks" -type f -path '*/Versions/A/*' \
         ! -name '*.plist' ! -name '*.txt' -print0)

# 6b. Loose .dylib files (Qt plugin shims, helper libs).
while IFS= read -r -d '' dylib; do
    codesign --force --sign "$SIGN_ID" --timestamp --options runtime \
        "$dylib"
done < <(find "$APP/Contents/Frameworks" -type f -name '*.dylib' -print0)

# 6c. Qt plugins (image format readers, platform shims, etc).
if [ -d "$APP/Contents/PlugIns" ]; then
    while IFS= read -r -d '' plugin; do
        codesign --force --sign "$SIGN_ID" --timestamp --options runtime \
            "$plugin"
    done < <(find "$APP/Contents/PlugIns" -type f -name '*.dylib' -print0)
fi

# 6d. mr-agent — separate executable, not part of any Qt framework.
codesign --force --sign "$SIGN_ID" --timestamp --options runtime \
    --entitlements "$ENTITLEMENTS" \
    "$APP/Contents/MacOS/mr-agent"

# 6e. Finally, the .app shell with main executable's entitlements.
codesign --force --sign "$SIGN_ID" --timestamp --options runtime \
    --entitlements "$ENTITLEMENTS" \
    "$APP"

echo "==> Verifying signature"
codesign --verify --deep --strict --verbose=2 "$APP" 2>&1 | tail -3

echo "==> Pre-notarization Gatekeeper check (rejection here is expected)"
spctl --assess --type exec --verbose "$APP" 2>&1 || true

# ---------------------------------------------------------------------------
# 7. DMG — drag-to-Applications layout.
# ---------------------------------------------------------------------------
echo "==> [7/8] Building DMG"
mkdir -p "$RELEASE_DIR"
rm -f "$DMG"

DMG_STAGING="$(mktemp -d)/${APP_NAME}-${VERSION}"
mkdir -p "$DMG_STAGING"
cp -R "$APP" "$DMG_STAGING/"
ln -s /Applications "$DMG_STAGING/Applications"

hdiutil create \
    -volname "${APP_NAME} ${VERSION}" \
    -srcfolder "$DMG_STAGING" \
    -ov \
    -format UDZO \
    "$DMG"

rm -rf "$(dirname "$DMG_STAGING")"

codesign --force --sign "$SIGN_ID" --timestamp "$DMG"

# ---------------------------------------------------------------------------
# 8. Notarize + staple, if creds are stored.
# ---------------------------------------------------------------------------
if xcrun notarytool history --keychain-profile "$NOTARY_PROFILE" \
        > /dev/null 2>&1; then
    echo "==> [8/8] Submitting DMG to Apple notary (1-15 min)"
    xcrun notarytool submit "$DMG" \
        --keychain-profile "$NOTARY_PROFILE" \
        --wait

    echo "==> Stapling .app and DMG"
    xcrun stapler staple "$APP"
    xcrun stapler staple "$DMG"

    echo "==> Re-verifying after staple"
    spctl --assess --type exec --verbose "$APP"
    spctl --assess --type install --context context:primary-signature \
        --verbose "$DMG" || true
else
    echo
    echo "    NOTE: notarytool profile '$NOTARY_PROFILE' not found."
    echo "    DMG is signed but unnotarized — Gatekeeper will warn other Macs."
    echo "    To enable notarization once:"
    echo
    echo "      xcrun notarytool store-credentials $NOTARY_PROFILE \\"
    echo "          --apple-id <your-apple-id> \\"
    echo "          --team-id $TEAM_ID \\"
    echo "          --password <app-specific-password>"
    echo
fi

echo
echo "==> Done"
echo "    .app: $APP"
echo "    DMG:  $DMG"
echo "    SHA:  $(shasum -a 256 "$DMG" | awk '{print $1}')"
