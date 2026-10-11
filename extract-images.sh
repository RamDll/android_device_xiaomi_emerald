#!/bin/bash
#
# SPDX-FileCopyrightText: The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#
# Copies the stock vendor-side partition images that clean-1 ships unchanged
# (BOARD_PREBUILT_*IMAGE) into vendor/xiaomi/emerald-images and checks them against
# images.sha256.
#
# Usage: ./extract-images.sh <dir>
#   <dir> holds the stock partitions as <name>.img or <name>_a.img, e.g. the output of
#   `lpunpack.py super.img <dir>` for a fastboot package, or `ota_extractor` for a recovery ROM.
#

set -e

MY_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ANDROID_ROOT="${MY_DIR}/../../.."
OUT_DIR="${ANDROID_ROOT}/vendor/xiaomi/emerald-images"
SRC_DIR="$1"

if [ -z "${SRC_DIR}" ] || [ ! -d "${SRC_DIR}" ]; then
    echo "usage: $0 <dir with stock partition images>" >&2
    exit 1
fi

mkdir -p "${OUT_DIR}"

grep -v '^#' "${MY_DIR}/images.sha256" | while read -r SHA256 IMAGE; do
    NAME="${IMAGE%.img}"
    if [ -f "${SRC_DIR}/${NAME}_a.img" ]; then
        SRC="${SRC_DIR}/${NAME}_a.img"
    elif [ -f "${SRC_DIR}/${NAME}.img" ]; then
        SRC="${SRC_DIR}/${NAME}.img"
    else
        echo "missing: ${NAME}.img / ${NAME}_a.img in ${SRC_DIR}" >&2
        exit 1
    fi

    cp "${SRC}" "${OUT_DIR}/${IMAGE}"
    echo "${SHA256}  ${OUT_DIR}/${IMAGE}" | sha256sum --check --quiet
    echo "${IMAGE}: ok"
done
