#!/usr/bin/env -S PYTHONPATH=../../../tools/extract-utils python3
#
# SPDX-FileCopyrightText: The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#
# Extracts the system-side MediaTek blobs (proprietary-files.txt) and the firmware images
# (proprietary-firmware.txt) from a stock OS3.0.305.0.WNFMIXM dump. The vendor-side partitions
# come as whole stock images, see extract-images.sh.
#

from extract_utils.fixups_blob import (
    blob_fixup,
    blob_fixups_user_type,
)
from extract_utils.main import (
    ExtractUtils,
    ExtractUtilsModule,
)

namespace_imports = [
    'hardware/mediatek',
]

blob_fixups: blob_fixups_user_type = {
    # libsink.so is renamed to libsink-mtk.so (as LineageOS motorola/mt6768-common)
    'system_ext/lib64/libimsma.so': blob_fixup()
        .replace_needed('libsink.so', 'libsink-mtk.so'),
}  # fmt: skip

module = ExtractUtilsModule(
    'emerald',
    'xiaomi',
    blob_fixups=blob_fixups,
    namespace_imports=namespace_imports,
    add_firmware_proprietary_file=True,
)

if __name__ == '__main__':
    utils = ExtractUtils.device(module)
    utils.run()
