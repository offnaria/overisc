#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Verilator をソースからビルドして <prefix> に入れる（CI 用。手元の版に合わせる）。
# 使い方: install-verilator.sh <prefix>
set -euo pipefail

VERSION=v5.026
prefix=${1:?usage: $0 <prefix>}

if [[ -x ${prefix}/bin/verilator ]]; then
    echo "already installed: $(${prefix}/bin/verilator --version)"
    exit 0
fi

src=$(mktemp -d)
trap 'rm -rf "${src}"' EXIT
git clone --depth 1 --branch "${VERSION}" https://github.com/verilator/verilator.git "${src}"
cd "${src}"
autoconf
./configure --prefix="${prefix}"
make -j"$(nproc)"
make install
"${prefix}/bin/verilator" --version
