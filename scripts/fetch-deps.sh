#!/usr/bin/env bash
# 拉取第三方依赖到 third_party/（固定版本，浅克隆）
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p third_party

clone() {  # clone <目录> <仓库> <tag> [sparse 路径...]
    local dir=$1 repo=$2 tag=$3; shift 3
    if [ -d "third_party/$dir/.git" ]; then echo "已存在: $dir"; return; fi
    if [ $# -gt 0 ]; then
        git clone --depth 1 --branch "$tag" --filter=blob:none --sparse \
            "https://github.com/$repo.git" "third_party/$dir"
        git -C "third_party/$dir" sparse-checkout set "$@"
    else
        git clone --depth 1 --branch "$tag" "https://github.com/$repo.git" "third_party/$dir"
    fi
}

clone cmsis_core      STMicroelectronics/cmsis_core           v5.9.0_20250520  Include
clone cmsis_device_f4 STMicroelectronics/cmsis_device_f4      v2.6.9
clone hal_f4          STMicroelectronics/stm32f4xx_hal_driver v1.8.5
clone fatfs           STMicroelectronics/stm32_mw_fatfs       r0.16_stm32cube_20260904
