#!/bin/bash

# SPDX-FileCopyrightText: 2019-2022 Joonas Javanainen <joonas.javanainen@gmail.com>
#
# SPDX-License-Identifier: MIT OR Apache-2.0

set -euo pipefail

RELEASE_FILE="cross-x86_64-unknown-linux-gnu.tar.gz"

mkdir -p ${HOME}/bin
curl -sSLO "https://github.com/cross-rs/cross/releases/download/v${CROSS_VERSION}/${RELEASE_FILE}"
echo "${CROSS_SHA256}  ${RELEASE_FILE}" | sha256sum
cat "${RELEASE_FILE}" | tar xzv -C "${HOME}/bin"
