#!/usr/bin/env bash
set -euo pipefail

PREFIX="${PREFIX:-$HOME/.local/uamashell}"
echo "Instalando en $PREFIX"

mkdir -p "$PREFIX/bin" "$PREFIX/etc" "$PREFIX/var/log"

make -C "$(dirname "$0")/.." all

cp -f "$(dirname "$0")/../bin/uamashell" "$PREFIX/bin/"
cp -f "$(dirname "$0")/../etc/uamashell.conf" "$PREFIX/etc/"
# no sobreescribir bitácoras existentes
touch "$PREFIX/var/log/uamashell.log" "$PREFIX/var/log/uamashell_error.log"

echo "Listo. Agrega a tu PATH: export PATH=\"$PREFIX/bin:\$PATH\""
echo "Config: $PREFIX/etc/uamashell.conf"
