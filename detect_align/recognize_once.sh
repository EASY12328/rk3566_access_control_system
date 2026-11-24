#!/bin/sh
# Diagnostic only: print a similarity score, never actuate a lock.
set -eu
package=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
export LD_LIBRARY_PATH="$package/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
reference="$package/board_feature.f32"
if [ ! -r "$reference" ]; then
    echo "Missing enrolled feature: $reference" >&2
    exit 1
fi
for name in detect_face face_align sface_extract compare_feature detector.rknn sface_rk3566_fp16.rknn; do
    if [ ! -e "$package/$name" ]; then
        echo "Missing required file: $package/$name" >&2
        exit 1
    fi
done
if [ ! -x /userdata/my_camera ]; then
    echo "Missing executable /userdata/my_camera" >&2
    exit 1
fi
work=$(mktemp -d "$package/capture.XXXXXX")
cd "$work"
echo "Capturing a fresh frame; face the camera..."
/userdata/my_camera
test -s frame.bmp
"$package/detect_face" "$package/detector.rknn" frame.bmp
"$package/face_align" detected_input.rgb 1280 720 detected_points.txt aligned.rgb
"$package/sface_extract" "$package/sface_rk3566_fp16.rknn" aligned.rgb feature.f32
"$package/compare_feature" "$reference" feature.f32
echo "Diagnostic files: $work"
