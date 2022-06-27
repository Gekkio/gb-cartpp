#!/bin/bash

# SPDX-FileCopyrightText: 2019-2022 Joonas Javanainen <joonas.javanainen@gmail.com>
#
# SPDX-License-Identifier: MIT OR Apache-2.0

set -euo pipefail

BASE=$(dirname ${0})

make -C "${BASE}/GB-CARTPP.X" all

TMPDIR=$(mktemp -d)
trap "rm -rf ${TMPDIR}" EXIT

XC_HEXFILE="${BASE}/GB-CARTPP.X/dist/GB_CARTPP_XC/production/GB-CARTPP.X.production.hex"
XC_IMGFILE="${BASE}/GB-CARTPP-XC.img"
cp "${XC_HEXFILE}" "${TMPDIR}/GB-CARTPP-XC.hex"
gpg2 --openpgp -u "admin+gb-cartpp-xc@gekkio.fi" -b -a "${TMPDIR}/GB-CARTPP-XC.hex"

tar -cz -C "${TMPDIR}" -f "${XC_IMGFILE}" GB-CARTPP-XC.hex GB-CARTPP-XC.hex.asc
echo Wrote "${XC_IMGFILE}"

DIY_HEXFILE="${BASE}/GB-CARTPP.X/dist/GB_CARTPP_DIY/production/GB-CARTPP.X.production.hex"
DIY_IMGFILE="${BASE}/GB-CARTPP-DIY.img"
cp "${DIY_HEXFILE}" "${TMPDIR}/GB-CARTPP-DIY.hex"
gpg2 --openpgp -u "admin+gb-cartpp-xc@gekkio.fi" -b -a "${TMPDIR}/GB-CARTPP-DIY.hex"

tar -cz -C "${TMPDIR}" -f "${DIY_IMGFILE}" GB-CARTPP-DIY.hex GB-CARTPP-DIY.hex.asc
echo Wrote "${DIY_IMGFILE}"
