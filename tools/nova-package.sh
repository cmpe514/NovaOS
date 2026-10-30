#!/usr/bin/env bash
# Packages the built images with checksums and a build manifest: out/novaos-<version>-<target>.tar.gz
set -euo pipefail
version=$1 target=$2
cd "$(dirname "$0")/.."
name=novaos-$version-$target
stage=build/package/$name
rm -rf "$stage" && mkdir -p "$stage"
cp "out/$name.iso" "out/$name-recovery-default.iso" build/x86_64/novaos.elf "$stage/"
cp THIRD_PARTY_LICENSES "$stage/" 2>/dev/null || true
{
    echo "NovaOS $version ($target)"
    echo "source commit: $(git rev-parse HEAD 2>/dev/null || echo unknown)$(git diff --quiet 2>/dev/null || echo ' (with uncommitted changes)')"
    echo "built: $(date -u +%Y-%m-%dT%H:%M:%SZ) on $(uname -sm)"
    echo "toolchain:"
    for t in clang ld.lld grub-mkrescue xorriso; do echo "  $t: $($t --version 2>&1 | head -1)"; done
    echo "sha256:"
    (cd "$stage" && sha256sum -- *.iso novaos.elf | sed 's/^/  /')
} > "$stage/MANIFEST.txt"
tar -C build/package -czf "out/$name.tar.gz" "$name"
cp "$stage/MANIFEST.txt" "out/MANIFEST-$name.txt"
(cd out && sha256sum "$name.tar.gz" > "$name.tar.gz.sha256")
cat "out/MANIFEST-$name.txt"
