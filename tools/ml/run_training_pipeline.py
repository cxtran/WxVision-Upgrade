#!/usr/bin/env python3
"""
One-command ML pipeline for WxVision:
1) build labeled dataset from raw trend CSV
2) train model and export firmware header
"""

from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys
from typing import List


def run_cmd(cmd: List[str]) -> int:
    print("+", " ".join(cmd))
    proc = subprocess.run(cmd)
    return int(proc.returncode)


def find_latest_trend_csv(data_dir: pathlib.Path) -> pathlib.Path | None:
    candidates = sorted(data_dir.glob("trend_*.csv"), key=lambda p: p.stat().st_mtime, reverse=True)
    return candidates[0] if candidates else None


def cleanup_old_trend_files(data_dir: pathlib.Path, keep_csv: pathlib.Path) -> int:
    removed = 0
    keep_csv = keep_csv.resolve()
    keep_json = keep_csv.with_suffix(".json")
    for p in data_dir.glob("trend_*.*"):
        if p.suffix.lower() not in {".csv", ".json"}:
            continue
        rp = p.resolve()
        if rp == keep_csv or rp == keep_json:
            continue
        try:
            p.unlink()
            removed += 1
        except OSError as exc:
            print(f"Warning: failed to remove {p}: {exc}")
    return removed


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument(
        "--in-csv",
        help="Raw trend CSV from download_trend_log.py. If omitted, auto-pick newest trend_*.csv in --data-dir.",
    )
    p.add_argument("--data-dir", default="tools/ml/data", help="Directory containing downloaded trend CSV files.")
    p.add_argument("--dataset-csv", default="tools/ml/data/outlook_dataset.csv", help="Output dataset CSV path.")
    p.add_argument("--out-header", default="include/ml_model_generated.h", help="Output firmware model header path.")
    p.add_argument(
        "--metadata-out",
        default="tools/ml/data/model_metadata.json",
        help="Output JSON metadata path for training summary.",
    )
    p.add_argument("--horizon-min", type=int, default=180, help="Future horizon minutes used when building labels.")
    p.add_argument(
        "--min-class-support",
        type=int,
        default=30,
        help="Minimum rows required for a class to remain enabled in exported model.",
    )
    p.add_argument(
        "--min-enabled-classes",
        type=int,
        default=2,
        help="Fail pipeline if fewer than this many classes are enabled after gating.",
    )
    p.add_argument(
        "--min-accuracy",
        type=float,
        default=0.0,
        help="Fail pipeline if test-set accuracy from trainer is below this value (0..1). Default disabled.",
    )
    p.add_argument(
        "--min-weighted-f1",
        type=float,
        default=0.0,
        help="Fail pipeline if weighted F1 from trainer is below this value (0..1). Default disabled.",
    )
    p.add_argument(
        "--cleanup-old-data",
        action="store_true",
        default=True,
        help="After successful training, delete older trend_*.csv/json in --data-dir and keep only the input pair (default: enabled).",
    )
    p.add_argument(
        "--keep-old-data",
        action="store_true",
        help="Disable cleanup and keep all downloaded trend_*.csv/json files.",
    )
    args = p.parse_args()

    repo_root = pathlib.Path(__file__).resolve().parents[2]
    ml_dir = pathlib.Path(__file__).resolve().parent
    build_script = ml_dir / "build_outlook_dataset.py"
    train_script = ml_dir / "train_outlook_model.py"

    if args.in_csv:
        in_csv = pathlib.Path(args.in_csv)
    else:
        in_csv = find_latest_trend_csv(pathlib.Path(args.data_dir))
        if in_csv is None:
            print(f"No trend_*.csv found in: {args.data_dir}")
            return 2
        print(f"Using latest trend CSV: {in_csv}")

    if not in_csv.is_absolute():
        in_csv = (repo_root / in_csv).resolve()
    dataset_csv = pathlib.Path(args.dataset_csv)
    if not dataset_csv.is_absolute():
        dataset_csv = (repo_root / dataset_csv).resolve()
    out_header = pathlib.Path(args.out_header)
    if not out_header.is_absolute():
        out_header = (repo_root / out_header).resolve()
    metadata_out = pathlib.Path(args.metadata_out)
    if not metadata_out.is_absolute():
        metadata_out = (repo_root / metadata_out).resolve()

    if not in_csv.exists():
        print(f"Input CSV not found: {in_csv}")
        return 2

    cmd_build = [
        sys.executable,
        str(build_script),
        "--in-csv",
        str(in_csv),
        "--out-csv",
        str(dataset_csv),
        "--horizon-min",
        str(args.horizon_min),
    ]
    rc = run_cmd(cmd_build)
    if rc != 0:
        return rc

    cmd_train = [
        sys.executable,
        str(train_script),
        "--csv",
        str(dataset_csv),
        "--out",
        str(out_header),
        "--min-class-support",
        str(args.min_class_support),
        "--min-enabled-classes",
        str(args.min_enabled_classes),
        "--min-accuracy",
        str(args.min_accuracy),
        "--min-weighted-f1",
        str(args.min_weighted_f1),
        "--metadata-out",
        str(metadata_out),
    ]
    rc = run_cmd(cmd_train)
    if rc != 0:
        return rc

    do_cleanup = args.cleanup_old_data and not args.keep_old_data
    if do_cleanup:
        data_dir = pathlib.Path(args.data_dir)
        if not data_dir.is_absolute():
            data_dir = (repo_root / data_dir).resolve()
        removed = cleanup_old_trend_files(data_dir, in_csv)
        print(f"Removed old trend files: {removed}")

    print("Pipeline complete.")
    print(f"Dataset : {dataset_csv}")
    print(f"Header  : {out_header}")
    print(f"Metadata: {metadata_out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
