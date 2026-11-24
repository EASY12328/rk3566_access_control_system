"""Static RetinaFace + SFace comparison. Run inside the Ubuntu rknn environment.

RetinaFace preprocessing/decoding follows the original Pytorch_Retinaface
implementation (BGR, mean 104/117/123, anchor variances 0.1/0.2).
SFace alignment and feature extraction use OpenCV FaceRecognizerSF.
"""

import argparse
import json
from pathlib import Path

import cv2
import numpy as np


ROOT = Path(__file__).resolve().parents[1]
INPUT_SIZE = 320
DETECTION_SCORE = 0.8


def make_anchors():
    anchors = []
    for step, sizes in ((8, (16, 32)), (16, (64, 128)), (32, (256, 512))):
        cells = INPUT_SIZE // step
        for row in range(cells):
            for column in range(cells):
                for size in sizes:
                    anchors.append(((column + 0.5) * step / INPUT_SIZE,
                                    (row + 0.5) * step / INPUT_SIZE,
                                    size / INPUT_SIZE, size / INPUT_SIZE))
    return np.asarray(anchors, dtype=np.float32)


def detect_one_face(net, anchors, image, name):
    height, width = image.shape[:2]
    scale = min(INPUT_SIZE / width, INPUT_SIZE / height)
    resized_width = max(1, round(width * scale))
    resized_height = max(1, round(height * scale))
    left = (INPUT_SIZE - resized_width) // 2
    top = (INPUT_SIZE - resized_height) // 2
    canvas = np.full((INPUT_SIZE, INPUT_SIZE, 3), 114, dtype=np.uint8)
    canvas[top:top + resized_height, left:left + resized_width] = cv2.resize(
        image, (resized_width, resized_height), interpolation=cv2.INTER_AREA)
    blob = cv2.dnn.blobFromImage(canvas, scalefactor=1.0,
                                mean=(104, 117, 123), swapRB=False, crop=False)
    net.setInput(blob)
    outputs = net.forward(net.getUnconnectedOutLayersNames())
    tensors = {}
    for output in outputs:
        if output.ndim != 3 or output.shape[:2] != (1, len(anchors)):
            raise RuntimeError(f"Unexpected RetinaFace output shape: {output.shape}")
        tensors[output.shape[2]] = output[0]
    if set(tensors) != {2, 4, 10}:
        raise RuntimeError(f"Unexpected RetinaFace outputs: {list(tensors)}")

    scores = tensors[2][:, 1]
    selected = np.flatnonzero(scores >= DETECTION_SCORE)
    if len(selected) == 0:
        raise RuntimeError(f"{name}: no face above score {DETECTION_SCORE}; check the photo")
    priors = anchors[selected]
    offsets = tensors[4][selected]
    centers = priors[:, :2] + offsets[:, :2] * 0.1 * priors[:, 2:]
    sizes = priors[:, 2:] * np.exp(offsets[:, 2:] * 0.2)
    boxes = np.concatenate((centers - sizes / 2, sizes), axis=1) * INPUT_SIZE
    points = (priors[:, None, :2]
              + tensors[10][selected].reshape(-1, 5, 2) * 0.1
              * priors[:, None, 2:]) * INPUT_SIZE
    kept = np.asarray(cv2.dnn.NMSBoxes(boxes.tolist(), scores[selected].tolist(),
                                      DETECTION_SCORE, 0.4)).reshape(-1)
    if len(kept) != 1:
        raise RuntimeError(f"{name}: detected {len(kept)} faces; use a single-person photo")

    index = int(kept[0])
    # Undo padding and the actual resize, including rounding to integer dimensions.
    scale_xy = np.array([resized_width / width, resized_height / height])
    box = boxes[index].copy()
    box[:2] = (box[:2] - [left, top]) / scale_xy
    box[2:] /= scale_xy
    landmarks = (points[index] - [left, top]) / scale_xy
    # OpenCV expects x, y, width, height, five (x,y) landmarks, then score.
    face = np.concatenate((box, landmarks.reshape(-1),
                           [scores[selected[index]]])).astype(np.float32)
    if not np.isfinite(face).all():
        raise RuntimeError(f"{name}: detector returned non-finite coordinates")
    return face


def save_image(path, image):
    success, encoded = cv2.imencode(".png", image)
    if not success:
        raise RuntimeError(f"Could not encode {path}")
    encoded.tofile(str(path))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--detector", type=Path,
                        default=ROOT / "models/RetinaFace_mobile320.onnx")
    args = parser.parse_args()
    recognizer_path = ROOT / "models/face_recognition_sface_2021dec.onnx"
    for path in (args.detector, recognizer_path):
        if not path.is_file():
            raise FileNotFoundError(f"Model missing: {path}")
    detector = cv2.dnn.readNetFromONNX(str(args.detector))
    detector.setPreferableBackend(cv2.dnn.DNN_BACKEND_OPENCV)
    detector.setPreferableTarget(cv2.dnn.DNN_TARGET_CPU)
    recognizer = cv2.FaceRecognizerSF.create(str(recognizer_path), "")
    anchors = make_anchors()
    output_dir = ROOT / "outputs/face_compare"
    output_dir.mkdir(parents=True, exist_ok=True)
    features = {}
    detection_scores = {}

    for name in ("register", "same_person", "other_person"):
        path = ROOT / "data/faces" / f"{name}.png"
        if not path.is_file():
            raise FileNotFoundError(f"Photo missing: {path}")
        image = cv2.imdecode(np.fromfile(str(path), dtype=np.uint8), cv2.IMREAD_COLOR)
        if image is None:
            raise RuntimeError(f"Could not decode {path}")
        face = detect_one_face(detector, anchors, image, name)
        aligned = recognizer.alignCrop(image, face)
        feature = recognizer.feature(aligned).copy()
        if not np.isfinite(feature).all() or np.linalg.norm(feature) == 0:
            raise RuntimeError(f"{name}: invalid face feature")
        features[name] = feature
        detection_scores[name] = float(face[-1])
        save_image(output_dir / f"{name}_aligned.png", aligned)
        preview = image.copy()
        x, y, width, height = face[:4]
        cv2.rectangle(preview, (round(x), round(y)),
                      (round(x + width), round(y + height)), (0, 255, 0), 2)
        for px, py in face[4:14].reshape(5, 2):
            cv2.circle(preview, (round(float(px)), round(float(py))), 3, (0, 128, 255), -1)
        save_image(output_dir / f"{name}_detected.png", preview)
        print(f"{name}: detection score={face[-1]:.4f}, feature size={feature.size}", flush=True)

    similarities = {}
    for name in ("same_person", "other_person"):
        score = float(recognizer.match(features["register"], features[name],
                                       cv2.FaceRecognizerSF_FR_COSINE))
        similarities[name] = score
        print(f"register vs {name}: cosine similarity = {score:.4f}", flush=True)
    report = {"detection_scores": detection_scores,
              "cosine_similarities": similarities,
              "note": "Diagnostic comparisons only; no access-control threshold is calibrated."}
    (output_dir / "scores.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"Saved previews, aligned faces and scores to: {output_dir}")


if __name__ == "__main__":
    main()
