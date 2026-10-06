#!/bin/sh
# diskos-keys.sh - make YOUR diskOS update keys (run once, on your own computer; needs openssl).
#
#   sh diskos-keys.sh [folder]        default folder: ./diskos-keys
#
# Makes:
#   root.key         SECRET. Signs leaf keys. Keep it offline (USB stick, password manager). Not needed for daily use.
#   leaf.key         SECRET. Signs every update (used by diskos-sign.sh). Anyone with it can update your Disc.
#   leafpub.pem, keyauthz, keyauthz.sig   public: the root key's approval of the leaf key (go into every update)
#   diskos-root.pub.pem   public: copy into the installer's payload/ folder and flash ONCE. The Disc then accepts
#                         only updates signed with these keys.
set -eu
DIR=${1:-./diskos-keys}
command -v openssl >/dev/null || { echo "openssl not found - install it (e.g. sudo apt install openssl)" >&2; exit 1; }
[ -e "$DIR/root.key" ] && { echo "$DIR/root.key already exists - refusing to overwrite your keys" >&2; exit 1; }
mkdir -p "$DIR"; chmod 700 "$DIR"; cd "$DIR"
umask 077
openssl ecparam -name prime256v1 -genkey -noout -out root.key
openssl ec -in root.key -pubout -out root.pub.pem 2>/dev/null
openssl ecparam -name prime256v1 -genkey -noout -out leaf.key
openssl ec -in leaf.key -pubout -out leafpub.pem 2>/dev/null
umask 022
LEAFSHA=$(sha256sum leafpub.pem | cut -d' ' -f1)
printf 'diskos-keyauthz-v1\nkeyid=owner-leaf-1\nkeyepoch=1\nrole=tier-a\npubkey_sha256=%s\n' "$LEAFSHA" > keyauthz
openssl dgst -sha256 -sign root.key -out keyauthz.sig keyauthz
openssl dgst -sha256 -verify root.pub.pem -signature keyauthz.sig keyauthz >/dev/null
cp root.pub.pem diskos-root.pub.pem
chmod 600 root.key leaf.key
echo "Keys made in $(pwd)"
echo "1. Copy diskos-root.pub.pem into the installer's payload/ folder and flash once."
echo "2. Back up root.key and leaf.key somewhere safe. Lose them = no more SD updates until you reflash new keys."
