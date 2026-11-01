#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Verible のビルド済みバイナリ（x86_64）を <prefix> に入れる（CI 用。手元の版に合わせる）。
# 使い方: install-verible.sh <prefix>   → <prefix>/bin/verible-verilog-format
set -euo pipefail

VERSION=v0.0-4007-g98bdb38a
SHA256=f34f5ff4b299ae7b4c8d64b7423d95250d5f54328ac98e32cdb0a0e7924d0d58
NAME=verible-${VERSION}-linux-static-x86_64.tar.gz
URL=https://github.com/chipsalliance/verible/releases/download/${VERSION}/${NAME}

prefix=${1:?usage: $0 <prefix>}

if [[ -x ${prefix}/bin/verible-verilog-format ]]; then
    echo "already installed: ${prefix}/bin/verible-verilog-format"
    exit 0
fi

tmp=$(mktemp -d)
trap 'rm -rf "${tmp}"' EXIT
curl -fL --retry 3 -o "${tmp}/${NAME}" "${URL}"
echo "${SHA256}  ${tmp}/${NAME}" | sha256sum -c -
mkdir -p "${prefix}"
tar -C "${prefix}" --strip-components=1 -xzf "${tmp}/${NAME}"
"${prefix}/bin/verible-verilog-format" --version | head -1
