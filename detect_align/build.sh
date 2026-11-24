#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd "$(dirname "$0")/.." && pwd)"
model_zoo="$HOME/rknn_model_zoo"
sdk_dir="$HOME/lckfb"
compiler="${GCC_COMPILER:-$sdk_dir/prebuilts/gcc/linux-x86/aarch64/gcc-buildroot-9.3.0-2020.03-x86_64_aarch64-rockchip-linux-gnu/bin/aarch64-rockchip-linux-gnu}"
export LD_LIBRARY_PATH="$HOME/rk-host-libs/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export CC="${compiler}-gcc" CXX="${compiler}-g++"
export PATH="$HOME/tools/cmake-3.27.9-linux-x86_64/bin:$PATH"
output="$project_dir/build/detect_align_package"
cmake -S "$project_dir/detect_align" -B "$project_dir/build/detect_align_build" \
    -DCMAKE_SYSTEM_NAME=Linux -DMODEL_ZOO="$model_zoo" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$output"
cmake --build "$project_dir/build/detect_align_build" --parallel 2
cmake --install "$project_dir/build/detect_align_build"
install -m 0644 "$project_dir/models/RetinaFace_mobile320.rknn" "$output/detector.rknn"
install -m 0644 "$project_dir/models/sface_rk3566_fp16.rknn" "$output/"
install -m 0644 "$project_dir/data/faces/board_feature.f32" "$output/"
echo "Package ready: $output"
