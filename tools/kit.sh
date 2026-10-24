#!/usr/bin/env bash
# kit.sh <node> - build HISTSH on <node> from clean, make the PCSI kit there and
# fetch it to out/kits/, with SHA256SUMS. (After vms-steward's kit.sh.)
#
# The version comes from src/core/hs_version.h alone; MAKE_KIT.COM refuses to
# package an image that reports a different one.
set -euo pipefail

top=$(cd "$(dirname "$0")/.." && pwd)
node=${1:?usage: kit.sh <node>}
: "${KIT_PRODUCER:=ISSINOHO}"
KIT_PRODUCT=HISTSH
version=$(sed -n 's/^#define HS_VERSION *"\(.*\)"/\1/p' "$top/src/core/hs_version.h")
pcsiversion=$(sed -n 's/^#define HS_PCSI_VERSION *"\(.*\)"/\1/p' "$top/src/core/hs_version.h")
[ -n "$version" ] && [ -n "$pcsiversion" ] || { echo "kit: no version in hs_version.h" >&2; exit 1; }
read -r _ ARCH _ _ _ _ _ < <(awk -v n="$node" '$1==n' "$top/tools/nodes.conf")
case $ARCH in [Xx]86*) base=X86VMS; archname=x86-64 ;; *) base=I64VMS; archname=IA64 ;; esac

# Generated kit inputs: never committed (they carry the version), uploaded to [.KIT].
stage=$top/out/kitstage
rm -rf "$stage"; mkdir -p "$stage" "$top/out/kits"
subst() { sed -e "s/@PRODUCER@/$KIT_PRODUCER/g" -e "s/@BASE@/$base/g" \
              -e "s/@PCSIVERSION@/$pcsiversion/g" -e "s/@VERSION@/$version/g" -e "s/@ARCH@/$archname/g"; }
subst < "$top/kit/histsh.pcsi\$desc_template" > "$stage/PRODUCT-$base.PCSI\$DESC"
subst < "$top/kit/histsh.pcsi\$text_template" > "$stage/PRODUCT-$base.PCSI\$TEXT"
subst < "$top/kit/readme.vms" > "$stage/README.VMS"
printf 'KIT_PRODUCER=%s\nKIT_PRODUCT=%s\nPCSI_VERSION=%s\nKIT_VERSION=%s\n' \
    "$KIT_PRODUCER" "$KIT_PRODUCT" "$pcsiversion" "$version" > "$stage/KIT.ENV"

echo "kit: HISTSH $version ($KIT_PRODUCER $base $KIT_PRODUCT $pcsiversion) on $node"
"$top/tools/push.sh" "$node" > /dev/null
"$top/tools/vms.sh" "$node" put "$stage"/* -- kit > /dev/null
"$top/tools/vms.sh" "$node" dcl '@build_vms' > "$top/out/kit-build-$node.log" 2>&1
grep -q 'build: \[.build\]histsh.exe' "$top/out/kit-build-$node.log" ||
    { tail -20 "$top/out/kit-build-$node.log"; echo "kit: build failed" >&2; exit 1; }
"$top/tools/vms.sh" "$node" dcl '@[.kit]make_kit' | tee "$top/out/kit-$node.log"
kit=$(sed -n 's/^MAKE_KIT: kit .*\]\([^;]*\);.*/\1/p' "$top/out/kit-$node.log" | head -1)
[ -n "$kit" ] || { echo "kit: no kit produced (see out/kit-$node.log)" >&2; exit 1; }
"$top/tools/vms.sh" "$node" get "KIT_$ARCH/$kit" "$top/out/kits/$kit"
# SHA256SUMS for this kit alone: out/kits/ also keeps earlier ones.
(cd "$top/out/kits" && sha256sum "$kit" > SHA256SUMS && cat SHA256SUMS)
