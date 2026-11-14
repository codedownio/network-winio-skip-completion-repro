#!/usr/bin/env bash
# Cross-compile with nixpkgs' mingw-w64 toolchain (or build sync_completion.c with any mingw gcc:
# gcc -O1 -o sync_completion.exe sync_completion.c -lws2_32)
set -euo pipefail
out=$(nix build --no-link --print-out-paths --impure --expr "let p = (import <nixpkgs> {}).pkgsCross.mingwW64; in p.runCommandCC \"sync-completion\" {} \"mkdir -p \$out/bin; \$CC -O1 -o \$out/bin/sync_completion.exe \${$PWD/sync_completion.c} -lws2_32; cp \${p.windows.mcfgthreads}/bin/*.dll \$out/bin/\"")
cp -f "$out"/bin/* . && chmod +w ./*.exe ./*.dll
echo "built ./sync_completion.exe"
