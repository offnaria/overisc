#!/usr/bin/env bash
# kernel.org crosstool の RISC-V GCC（libc なし）を取得して展開する（D-33）。
# 使い方: get-toolchain.sh <インストール先>
#   <インストール先>/gcc-<版>-nolibc/riscv64-linux/bin/riscv64-linux-gcc ができる。
# 版を上げるときは GCC_VER と SHA256 を一緒に更新し、署名（.sign）を確認すること。
set -euo pipefail

GCC_VER=16.2.0
SHA256=dd32c8e817474b3815b3e18e92c6a15e32e3860caa92e289b84f79b24b82e5a4
NAME=x86_64-gcc-${GCC_VER}-nolibc-riscv64-linux
URL=https://mirrors.edge.kernel.org/pub/tools/crosstool/files/bin/x86_64/${GCC_VER}/${NAME}.tar.xz

dest=${1:?usage: $0 <install dir>}
bin=${dest}/gcc-${GCC_VER}-nolibc/riscv64-linux/bin

if [[ -x ${bin}/riscv64-linux-gcc ]]; then
    echo "already installed: ${bin}"
    exit 0
fi

mkdir -p "${dest}"
tmp=$(mktemp -d)
trap 'rm -rf "${tmp}"' EXIT

curl -fL --retry 3 -o "${tmp}/${NAME}.tar.xz" "${URL}"
echo "${SHA256}  ${tmp}/${NAME}.tar.xz" | sha256sum -c -
tar -C "${dest}" -xf "${tmp}/${NAME}.tar.xz"
echo "installed: ${bin}"
