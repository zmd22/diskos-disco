#!/bin/sh
# diskos-sign.sh - turn a built mq_ui into a signed SD-card update (needs openssl and the keys from diskos-keys.sh).
#
#   sh diskos-sign.sh path/to/mq_ui [version] [keys-folder] [out-folder]
#      version      label shown on the Disc, e.g. 1.2.1-test (letters, digits, . - _); default 1.2.0-<epoch>
#      keys-folder  default ./diskos-keys
#      out-folder   default ./diskos-update  -> copy this FOLDER to the root of the SD card
#
# On the Disc: Settings > System > Update from SD Card > Update, then restart. The epoch is UTC minutes
# since 2026: runs within the same minute share an epoch. An accepted update requires a later epoch.
set -eu
UI=${1:?usage: diskos-sign.sh path/to/mq_ui [version] [keys-folder] [out-folder]}
KEYS=${3:-./diskos-keys}; OUT=${4:-./diskos-update}
command -v openssl >/dev/null || { echo "openssl not found" >&2; exit 1; }
for f in leaf.key leafpub.pem keyauthz keyauthz.sig; do [ -f "$KEYS/$f" ] || { echo "missing $KEYS/$f - run diskos-keys.sh first" >&2; exit 1; }; done
[ "$(od -An -tx1 -N4 "$UI" | tr -d ' ')" = 7f454c46 ] && [ "$(od -An -tx1 -j18 -N2 "$UI" | tr -d ' ')" = 0800 ] \
    || { echo "$UI is not a MIPS mq_ui binary" >&2; exit 1; }
EPOCH=$(( ($(date -u +%s) - 1767225600) / 60 ))
VER=${2:-1.2.0-$EPOCH}
case "$VER" in ''|*[!A-Za-z0-9._-]*) echo "version may only use letters, digits, . - _" >&2; exit 1;; esac
[ ${#VER} -le 32 ] || { echo "version too long (max 32)" >&2; exit 1; }
SIZE=$(wc -c < "$UI" | tr -d ' '); SHA=$(sha256sum "$UI" | cut -d' ' -f1)
rm -rf "$OUT"; mkdir -p "$OUT"
cp "$UI" "$OUT/mq_ui"
cp "$KEYS/leafpub.pem" "$KEYS/keyauthz" "$KEYS/keyauthz.sig" "$OUT/"
printf 'diskos-update-v1\ncomponent=mq_ui\nepoch=%s\nversion=%s\nsize=%s\nsha256=%s\nchannel=sd\n' "$EPOCH" "$VER" "$SIZE" "$SHA" > "$OUT/update.manifest"
openssl dgst -sha256 -sign "$KEYS/leaf.key" -out "$OUT/update.manifest.sig" "$OUT/update.manifest"
openssl dgst -sha256 -verify "$KEYS/leafpub.pem" -signature "$OUT/update.manifest.sig" "$OUT/update.manifest" >/dev/null
echo "Signed $VER (epoch $EPOCH, mq_ui md5 $(md5sum "$UI" | cut -d' ' -f1))"
echo "Copy the folder $OUT to the root of the SD card (it must be <SD>/diskos-update/)."
