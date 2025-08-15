#!/usr/bin/env bash
#!/usr/bin/env bash
set -euo pipefail

NAME="KERNEL_FORCE_DIRECTORIO_VERSION_3"
VERSION="${VERSION:-III}"
DIRNAME="${NAME}_VERSION_${VERSION}"

TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

# Crear la carpeta de paquete
mkdir -p "$TMPDIR/$DIRNAME"

# COPIA sólo tu proyecto, no todo tu home:
cp -a . "$TMPDIR/$DIRNAME"

# Empaquetar y comprimir
tar -C "$TMPDIR" -cvf "${DIRNAME}.tar" "$DIRNAME"
gzip -9v "${DIRNAME}.tar"
mv "${DIRNAME}.tar.gz" "${DIRNAME}.tgz"

echo "Paquete generado: ${DIRNAME}.tgz"

