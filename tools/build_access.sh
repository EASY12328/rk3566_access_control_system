#!/usr/bin/env bash
set -euo pipefail

source_dir="$(cd "$(dirname "$0")" && pwd)"
project_dir="$(dirname "$source_dir")"
sdk_dir="${RK_SDK_DIR:-$HOME/lckfb}"
br_output="$sdk_dir/buildroot/output/rockchip_rk3566"
build_dir="$project_dir/build/qt_preview_build"
package_dir="$project_dir/build/qt_preview_package"
export PATH="$br_output/host/bin:$PATH"

# qmake must run on Ubuntu and use the board's AArch64 Qt SDK.
qmake="${QMAKE:-}"
if [[ -z "$qmake" ]]; then
    for candidate in "$br_output/host/bin/qmake" "$br_output/host/usr/bin/qmake"; do
        if [[ -x "$candidate" ]]; then
            qmake="$candidate"
            break
        fi
    done
fi
if [[ -z "$qmake" ]]; then
    echo "未在 RK3566 Buildroot SDK 的 host/bin 或 host/usr/bin 找到 qmake。" >&2
    echo '运行 bash tools/build_qt_sdk.sh 构建 Qt 开发环境和门禁程序。' >&2
    exit 1
fi
qmake="$(command -v "$qmake")"
version="$("$qmake" -query QT_VERSION)"
echo "qmake: $qmake"
echo "Qt version: $version"
echo "Target spec: $("$qmake" -query QMAKE_XSPEC)"
echo "Target headers: $("$qmake" -query QT_INSTALL_HEADERS)"
if [[ "$version" != 5.15.2 ]]; then
    echo "当前板子使用 Qt 5.15.2，请使用对应的交叉编译 SDK。" >&2
    exit 1
fi
if [[ "${1:-}" == "--check" ]]; then
    exit 0
fi

export LD_LIBRARY_PATH="$HOME/rk-host-libs/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
mkdir -p "$build_dir"
cd "$build_dir"
qmake_spec=()
if [[ "$qmake" == "$br_output"/host/* ]]; then
    qmake_spec=(-spec devices/linux-buildroot-g++)
fi
"$qmake" "${qmake_spec[@]}" "$project_dir/code/ui/access_preview.pro" -o Makefile
make -j2
if ! LC_ALL=C readelf -h access_preview | awk '/Machine:/ && /AArch64/ { found = 1 } END { exit !found }'; then
    echo "编译产物不是 AArch64 ELF，请检查 qmake 的目标配置。" >&2
    exit 1
fi
mkdir -p "$package_dir"
install -m 0755 access_preview "$package_dir/access_preview"
install -m 0755 "$source_dir/run_access.sh" "$package_dir/run.sh"
echo "Package ready: $package_dir"
