#!/usr/bin/env bash
# Packages the built images with checksums and a build manifest: out/novaos-<version>-<target>.tar.gz
set -euo pipefail
version=$1 target=$2; shift 2   # remaining arguments: the image files to include
cd "$(dirname "$0")/.."
arch=${target#qemu-}
name=novaos-$version-$target
stage=build/package/$name
rm -rf "$stage" && mkdir -p "$stage"
cp "$@" "build/$arch/novaos.elf" "$stage/"
cp THIRD_PARTY_LICENSES "$stage/" 2>/dev/null || true
{
    echo "NovaOS $version ($target)"
    echo "source commit: $(git rev-parse HEAD 2>/dev/null || echo unknown)$(git diff --quiet 2>/dev/null || echo ' (with uncommitted changes)')"
    echo "built: $(date -u +%Y-%m-%dT%H:%M:%SZ) on $(uname -sm)"
    echo "toolchain:"
    for t in clang ld.lld grub-mkrescue xorriso; do echo "  $t: $($t --version 2>&1 | head -1)"; done
    echo "sha256:"
    (cd "$stage" && for f in *; do case $f in MANIFEST.txt|THIRD_PARTY_LICENSES) ;; *) sha256sum -- "$f" ;; esac; done | sed "s/^/  /")
} > "$stage/MANIFEST.txt"
tar -C build/package -czf "out/$name.tar.gz" "$name"
cp "$stage/MANIFEST.txt" "out/MANIFEST-$name.txt"
(cd out && sha256sum "$name.tar.gz" > "$name.tar.gz.sha256")
cat "out/MANIFEST-$name.txt"
