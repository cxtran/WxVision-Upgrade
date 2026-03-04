#!/usr/bin/env python3
"""
Train a lightweight multinomial logistic-regression outlook model and export
constants for ESP32 firmware (include/ml_model_generated.h).

Input CSV must include these feature columns:
  temp_delta_30m,temp_delta_3h,hum_delta_30m,hum_delta_3h,
  press_delta_30m,press_delta_3h,press_delta_24h,co2_delta_3h,hour_sin,hour_cos,
  plus optional extended features (see FEATURES list below)

And label column:
  label   (values listed in CLASSES below)
"""

from __future__ import annotations

import argparse
import json
import pathlib
import sys
from collections import Counter
from datetime import datetime, timezone


FEATURES = [
    # Keep the first 10 features in the legacy order for backward compatibility.
    "temp_delta_30m",
    "temp_delta_3h",
    "hum_delta_30m",
    "hum_delta_3h",
    "press_delta_30m",
    "press_delta_3h",
    "press_delta_24h",
    "co2_delta_3h",
    "hour_sin",
    "hour_cos",
    # Extended features.
    "temp_delta_6h",
    "temp_delta_12h",
    "hum_delta_6h",
    "press_delta_1h",
    "press_delta_6h",
    "co2_delta_30m",
    "co2_level_now",
    "dew_point_now",
    "dew_point_delta_3h",
    "lux_delta_30m",
    "day_night_flag",
    "rolling_std_press_3h",
]
CLASSES = [
    "RAIN POSS",
    "CLEARING",
    "UNSETTLED",
    "STEADY",
    "WARMING",
    "COOLING",
    "HUMIDIFYING",
    "DRYING",
    "STORM RISK",
    "FOG RISK",
    "VENTILATE",
    "HEAT STRESS",
    "COLD STRESS",
]


def fmt_float(v: float) -> str:
    return f"{float(v):.8f}f"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--csv", required=True, help="Training dataset CSV")
    parser.add_argument(
        "--out",
        default="include/ml_model_generated.h",
        help="Output C++ header path",
    )
    parser.add_argument(
        "--min-class-support",
        type=int,
        default=30,
        help="Minimum training rows required for a class to stay enabled in exported model.",
    )
    parser.add_argument(
        "--min-enabled-classes",
        type=int,
        default=2,
        help="Fail training if fewer than this many classes are enabled after gating.",
    )
    parser.add_argument(
        "--metadata-out",
        default="tools/ml/data/model_metadata.json",
        help="Output JSON metadata path for training summary.",
    )
    parser.add_argument(
        "--min-accuracy",
        type=float,
        default=0.0,
        help="Fail training if test-set accuracy is below this value (0..1). Default disabled.",
    )
    parser.add_argument(
        "--min-weighted-f1",
        type=float,
        default=0.0,
        help="Fail training if weighted F1 is below this value (0..1). Default disabled.",
    )
    args = parser.parse_args()

    try:
        import pandas as pd
        from sklearn.linear_model import LogisticRegression
        from sklearn.metrics import accuracy_score, classification_report, f1_score
        from sklearn.model_selection import train_test_split
    except Exception as exc:  # pragma: no cover
        print("Missing dependencies. Install with:")
        print("  pip install pandas scikit-learn")
        print(f"Error: {exc}")
        return 2

    csv_path = pathlib.Path(args.csv)
    out_path = pathlib.Path(args.out)
    metadata_out = pathlib.Path(args.metadata_out)

    if not csv_path.exists():
        print(f"CSV not found: {csv_path}")
        return 2

    df = pd.read_csv(csv_path)
    missing = [c for c in FEATURES + ["label"] if c not in df.columns]
    if missing:
        print("Missing columns:", ", ".join(missing))
        return 2

    df = df.dropna(subset=FEATURES + ["label"]).copy()
    if len(df) < 50:
        print("Need at least 50 labeled rows.")
        return 2

    # enforce class set order
    df["label"] = df["label"].astype(str).str.strip().str.upper()
    allowed = set(CLASSES)
    df = df[df["label"].isin(allowed)].copy()
    if len(df) < 50:
        print("Not enough rows after class filtering.")
        return 2

    x = df[FEATURES].astype(float).values
    means = x.mean(axis=0)
    scales = x.std(axis=0)
    scales[scales < 1e-6] = 1.0
    xn = (x - means) / scales

    class_to_idx = {name: idx for idx, name in enumerate(CLASSES)}
    y = df["label"].map(class_to_idx).astype(int).values
    class_counts = Counter(y.tolist())
    unique_classes = sorted(class_counts.keys())
    if len(unique_classes) < 2:
        print("Need at least 2 classes in dataset to train a classifier.")
        print("Class counts:", {CLASSES[int(k)]: int(v) for k, v in class_counts.items()})
        return 2

    # Stratified split is preferred, but it fails when a class has < 2 samples.
    stratify_y = y if min(class_counts.values()) >= 2 else None
    if stratify_y is None:
        print("Warning: at least one class has <2 rows; using non-stratified split.")
    x_train, x_test, y_train, y_test = train_test_split(xn, y, test_size=0.2, random_state=42, stratify=stratify_y)

    clf = LogisticRegression(
        solver="lbfgs",
        max_iter=1000,
        class_weight="balanced",
    )
    clf.fit(x_train, y_train)
    pred = clf.predict(x_test)
    all_labels = list(range(len(CLASSES)))
    report_text = classification_report(y_test, pred, labels=all_labels, target_names=CLASSES, zero_division=0)
    print(report_text)
    accuracy = float(accuracy_score(y_test, pred))
    weighted_f1 = float(f1_score(y_test, pred, labels=all_labels, average="weighted", zero_division=0))
    macro_f1 = float(f1_score(y_test, pred, labels=all_labels, average="macro", zero_division=0))

    # Remap model rows to fixed CLASSES order.
    # sklearn may return:
    # - multiclass: coef/intercept rows per class
    # - binary: a single row for classes_[1], classes_[0] is implied as the negated row
    model_params = {}
    if len(clf.classes_) == 2 and clf.coef_.shape[0] == 1:
        neg_class = int(clf.classes_[0])
        pos_class = int(clf.classes_[1])
        w = clf.coef_[0]
        b = float(clf.intercept_[0])
        model_params[pos_class] = (w, b)
        model_params[neg_class] = (-w, -b)
    else:
        class_to_row = {int(c): i for i, c in enumerate(clf.classes_)}
        for class_idx, row in class_to_row.items():
            model_params[class_idx] = (clf.coef_[row], float(clf.intercept_[row]))

    weights = []
    bias = []
    min_support = max(0, int(args.min_class_support))
    enabled_classes = []
    disabled_classes = []
    for class_name in CLASSES:
        class_idx = class_to_idx[class_name]
        class_support = int(class_counts.get(class_idx, 0))
        if class_support >= min_support and class_idx in model_params:
            w, b = model_params[class_idx]
            weights.append(w)
            bias.append(b)
            enabled_classes.append((class_name, class_support))
        else:
            # Class absent or under-supported in training data: keep it disabled.
            weights.append([0.0] * len(FEATURES))
            bias.append(-12.0)
            disabled_classes.append((class_name, class_support))

    min_enabled_classes = max(1, int(args.min_enabled_classes))
    enabled_count = len(enabled_classes)

    metadata = {
        "generatedAtUtc": datetime.now(timezone.utc).isoformat(),
        "datasetCsv": str(csv_path.resolve()),
        "outputHeader": str(out_path.resolve()),
        "featureCount": len(FEATURES),
        "classCount": len(CLASSES),
        "rowCount": int(len(df)),
        "minClassSupport": int(min_support),
        "minEnabledClasses": int(min_enabled_classes),
        "enabledClassCount": int(enabled_count),
        "classSupport": {name: int(class_counts.get(idx, 0)) for idx, name in enumerate(CLASSES)},
        "enabledClasses": [{"name": name, "support": int(count)} for name, count in enabled_classes],
        "disabledClasses": [{"name": name, "support": int(count)} for name, count in disabled_classes],
        "metrics": {
            "accuracy": accuracy,
            "weightedF1": weighted_f1,
            "macroF1": macro_f1,
        },
    }
    metadata_out.parent.mkdir(parents=True, exist_ok=True)
    metadata_out.write_text(json.dumps(metadata, indent=2), encoding="utf-8")
    print(f"Wrote metadata: {metadata_out}")

    if enabled_count < min_enabled_classes:
        print(
            f"Error: only {enabled_count} class(es) enabled; "
            f"need at least {min_enabled_classes}.")
        return 3

    min_accuracy = min(1.0, max(0.0, float(args.min_accuracy)))
    min_weighted_f1 = min(1.0, max(0.0, float(args.min_weighted_f1)))
    if accuracy < min_accuracy:
        print(f"Error: accuracy {accuracy:.3f} is below minimum {min_accuracy:.3f}")
        return 4
    if weighted_f1 < min_weighted_f1:
        print(f"Error: weighted F1 {weighted_f1:.3f} is below minimum {min_weighted_f1:.3f}")
        return 5

    out_lines = []
    out_lines.append("#pragma once")
    out_lines.append("")
    out_lines.append("// Auto-generated by tools/ml/train_outlook_model.py")
    out_lines.append("")
    out_lines.append("namespace wxv::ml::generated")
    out_lines.append("{")
    out_lines.append("constexpr bool kHasModel = true;")
    out_lines.append(f"constexpr int kFeatureCount = {len(FEATURES)};")
    out_lines.append(f"constexpr int kClassCount = {len(CLASSES)};")
    out_lines.append("")
    out_lines.append("constexpr const char *kFeatureNames[kFeatureCount] = {")
    for name in FEATURES:
        out_lines.append(f'    "{name}",')
    out_lines.append("};")
    out_lines.append(
        "constexpr float kFeatureMean[kFeatureCount] = {" + ", ".join(fmt_float(v) for v in means) + "};"
    )
    out_lines.append(
        "constexpr float kFeatureScale[kFeatureCount] = {" + ", ".join(fmt_float(v) for v in scales) + "};"
    )
    out_lines.append(
        "constexpr float kBias[kClassCount] = {" + ", ".join(fmt_float(v) for v in bias) + "};"
    )
    out_lines.append("constexpr float kWeights[kClassCount][kFeatureCount] = {")
    for row in weights:
        out_lines.append("    {" + ", ".join(fmt_float(v) for v in row) + "},")
    out_lines.append("};")
    out_lines.append("constexpr const char *kClassLabels[kClassCount] = {")
    for name in CLASSES:
        out_lines.append(f'    "{name}",')
    out_lines.append("};")
    out_lines.append("} // namespace wxv::ml::generated")
    out_lines.append("")

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text("\n".join(out_lines), encoding="utf-8")
    print(f"Wrote model header: {out_path}")
    print(f"Class gating threshold: min-class-support={min_support}")
    print(f"Minimum enabled classes required: {min_enabled_classes}")
    print(f"Metrics: accuracy={accuracy:.3f}, weighted_f1={weighted_f1:.3f}, macro_f1={macro_f1:.3f}")
    if min_accuracy > 0.0 or min_weighted_f1 > 0.0:
        print(f"Metric thresholds: min_accuracy={min_accuracy:.3f}, min_weighted_f1={min_weighted_f1:.3f}")
    if enabled_classes:
        print("Enabled classes:", ", ".join(f"{name}({count})" for name, count in enabled_classes))
    if disabled_classes:
        print("Disabled classes:", ", ".join(f"{name}({count})" for name, count in disabled_classes))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
