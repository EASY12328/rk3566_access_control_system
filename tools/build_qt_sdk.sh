#!/usr/bin/env bash
set -euo pipefail
umask 022

source_dir="$(cd "$(dirname "$0")" && pwd)"
project_dir="$(dirname "$source_dir")"
sdk_dir="${RK_SDK_DIR:-$HOME/lckfb}"
br_dir="$(cd "$sdk_dir/buildroot" && pwd -P)"
br_output="$br_dir/output/rockchip_rk3566"
base_config="$br_output/.config"
build_dir="$project_dir/build/qt_preview_sdk"

if [[ ! -f "$base_config" || ! -x "$br_output/host/bin/aarch64-buildroot-linux-gnu-g++" ]]; then
    echo "缺少已构建的 RK3566 Buildroot 配置或 AArch64 编译器：$br_output" >&2
    exit 1
fi
if ! grep -qx 'BR2_aarch64=y' "$base_config"; then
    echo "当前 Buildroot 配置不是 AArch64：$base_config" >&2
    exit 1
fi
if [[ -x "$br_output/host/bin/qmake" &&
      -r "$br_output/staging/usr/include/qt5/QtWidgets/QWidget" ]]; then
    exec env QMAKE="$br_output/host/bin/qmake" bash "$source_dir/build_access.sh"
fi

mkdir -p "$build_dir"
qt_config="$(cd "$build_dir" && pwd -P)/buildroot.config"
# Keep the original .config; use the existing toolchain and package output.
sed -E '/^(# )?BR2_PACKAGE_QT5(=| )/d; /^(# )?BR2_PACKAGE_QT5BASE(_(GUI|WIDGETS|OPENGL|OPENGL_DESKTOP|OPENGL_ES2|EXAMPLES|FONTCONFIG|DEFAULT_QPA))?(=| )/d' \
    "$base_config" > "$qt_config"
cat >> "$qt_config" <<'EOF'
BR2_PACKAGE_QT5=y
BR2_PACKAGE_QT5BASE=y
BR2_PACKAGE_QT5BASE_GUI=y
BR2_PACKAGE_QT5BASE_WIDGETS=y
BR2_PACKAGE_QT5BASE_OPENGL=y
BR2_PACKAGE_QT5BASE_OPENGL_ES2=y
BR2_PACKAGE_QT5BASE_FONTCONFIG=y
BR2_PACKAGE_QT5BASE_DEFAULT_QPA="wayland"
# BR2_PACKAGE_QT5BASE_EXAMPLES is not set
EOF

make_args=(-C "$br_dir" "O=$br_output" "BR2_CONFIG=$qt_config"
           "EXTRAMAKEARGS=O=$br_output BR2_CONFIG=$qt_config")
make "${make_args[@]}" olddefconfig
for symbol in BR2_PACKAGE_QT5BASE BR2_PACKAGE_QT5BASE_WIDGETS BR2_PACKAGE_QT5BASE_OPENGL_ES2; do
    if ! grep -qx "$symbol=y" "$qt_config"; then
        echo "Buildroot 没有启用 $symbol，请查看 $qt_config。" >&2
        exit 1
    fi
done
make "${make_args[@]}" silentoldconfig
echo "开始构建 Qt Base 开发环境，日志：$build_dir/qt5base.log"
make "${make_args[@]}" qt5base-install-staging 2>&1 | tee "$build_dir/qt5base.log"
exec env QMAKE="$br_output/host/bin/qmake" bash "$source_dir/build_access.sh"
