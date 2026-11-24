"""Convert SFace for RK3566 and compare its simulator features with OpenCV.

Run with Python in the Ubuntu rknn environment. --check-input only checks
OpenCV preprocessing and prepares reference inputs, without RKNN-Toolkit2.
"""

import argparse
import json
from pathlib import Path

import cv2
import numpy as np


ROOT = Path(__file__).resolve().parents[1]
MODEL = ROOT / "models/face_recognition_sface_2021dec.onnx"
OUTPUT = ROOT / "models/sface_rk3566_fp16.rknn"
NAMES = ("register", "same_person", "other_person")


def unit_feature(value):
    vector = np.asarray(value, dtype=np.float32).reshape(-1)
    norm = np.linalg.norm(vector)
    if vector.size != 128 or not np.isfinite(vector).all() or norm < 1e-12:
        raise RuntimeError("Expected a finite, nonzero 128-dimensional SFace feature")
    return vector / norm


def prepare_references():
    recognizer = cv2.FaceRecognizerSF.create(str(MODEL), "")
    network = cv2.dnn.readNetFromONNX(str(MODEL))
    inputs, references = {}, {}
    directory = ROOT / "outputs/sface_reference"
    directory.mkdir(parents=True, exist_ok=True)
    for name in NAMES:
        path = ROOT / "outputs/face_compare" / f"{name}_aligned.png"
        if not path.is_file():
            raise FileNotFoundError(f"Missing {path}; run python tools/compare_faces.py first")
        image = cv2.imdecode(np.fromfile(str(path), dtype=np.uint8), cv2.IMREAD_COLOR)
        if image is None or image.shape != (112, 112, 3):
            raise RuntimeError(f"Expected a 112x112 aligned face: {path}")
        # OpenCV SFace uses RGB values 0..255 with scale=1 and mean=0.
        # Preserve the preprocessing already embedded in the original graph.
        rgb = np.ascontiguousarray(cv2.cvtColor(image, cv2.COLOR_BGR2RGB))
        tensor = np.ascontiguousarray(rgb.transpose(2, 0, 1)[None], dtype=np.float32)
        network.setInput(tensor)
        direct = unit_feature(network.forward())
        reference = unit_feature(recognizer.feature(image))
        if not np.allclose(direct, reference, atol=1e-5, rtol=1e-4):
            raise RuntimeError(f"{name}: RGB input differs from OpenCV SFace preprocessing")
        inputs[name] = rgb
        references[name] = reference
        rgb.tofile(str(directory / f"{name}_rgb.bin"))
        np.save(str(directory / f"{name}_feature.npy"), reference)
        print(f"{name}: RGB preprocessing verified, feature size=128", flush=True)
    return inputs, references


def check_return(result, stage):
    if result != 0:
        raise RuntimeError(f"{stage} failed, return code: {result}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check-input", action="store_true")
    args = parser.parse_args()
    inputs, references = prepare_references()
    if args.check_input:
        return

    from rknn.api import RKNN

    rknn = RKNN(verbose=False)
    try:
        print("Configuring RK3566, RGB input, mean=0, std=1", flush=True)
        check_return(rknn.config(target_platform="rk3566", mean_values=[[0, 0, 0]],
                                 std_values=[[1, 1, 1]]), "config")
        check_return(rknn.load_onnx(model=str(MODEL)), "load_onnx")
        print("Building non-quantized RKNN model...", flush=True)
        check_return(rknn.build(do_quantization=False), "build")
        temporary = OUTPUT.with_name(OUTPUT.stem + ".pending.rknn")
        check_return(rknn.export_rknn(str(temporary)), "export_rknn")
        temporary.replace(OUTPUT)
        print(f"Exported: {OUTPUT}", flush=True)

        print("Checking PC simulator; hardware inference still needs separate validation", flush=True)
        check_return(rknn.init_runtime(), "init_runtime simulator")
        simulated = {}
        similarities = {}
        for name in NAMES:
            result = rknn.inference(inputs=[inputs[name][None]], data_format=["nhwc"])
            if result is None or len(result) != 1:
                raise RuntimeError(f"{name}: expected one output tensor")
            simulated[name] = unit_feature(result[0])
            similarities[name] = float(np.dot(references[name], simulated[name]))
            print(f"{name}: OpenCV vs simulator feature cosine={similarities[name]:.6f}", flush=True)

        comparisons = {}
        for name in NAMES[1:]:
            baseline = float(np.dot(references["register"], references[name]))
            converted = float(np.dot(simulated["register"], simulated[name]))
            comparisons[name] = {"opencv": baseline, "rknn_simulator": converted}
            print(f"register vs {name}: OpenCV={baseline:.4f}, simulator={converted:.4f}", flush=True)
        report = {"feature_cosines": similarities, "pair_comparisons": comparisons,
                  "note": "Simulator results only. RK3566 board validation is pending."}
        report_path = ROOT / "outputs/sface_reference/simulator_scores.json"
        report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
        # These are conversion-consistency checks, not identity decision thresholds.
        if min(similarities.values()) < 0.99 or any(
                abs(pair["opencv"] - pair["rknn_simulator"]) > 0.02
                for pair in comparisons.values()):
            raise RuntimeError("Conversion consistency check failed; inspect results before board deployment")
        print("Simulator consistency check passed. Next: verify the model on RK3566.")
    finally:
        rknn.release()


if __name__ == "__main__":
    main()
