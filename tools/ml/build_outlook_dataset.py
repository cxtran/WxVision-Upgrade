#!/usr/bin/env python3
"""
Build outlook training dataset from raw trend CSV exported from WxVision.

Input columns expected:
  ts,temp,hum,press,lux,co2

Output columns:
  temp_delta_30m,temp_delta_3h,hum_delta_30m,hum_delta_3h,
  press_delta_30m,press_delta_3h,press_delta_24h,co2_delta_3h,hour_sin,hour_cos,
  plus extended features listed in feature_fields, and label

Label strategy:
  "future"  -> label from future-horizon deltas (recommended bootstrap)
"""

from __future__ import annotations

import argparse
import csv
import math
import pathlib
from typing import Dict, List, Optional


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


def parse_float(v: str) -> float:
    try:
        return float(v)
    except Exception:
        return float("nan")


def is_valid(v: float, positive_only: bool = False) -> bool:
    if math.isnan(v):
        return False
    if positive_only and v <= 0.0:
        return False
    return True


def nearest_idx_at_or_before(rows: List[Dict], target_ts: int) -> Optional[int]:
    idx = None
    for i, r in enumerate(rows):
        if r["ts"] <= target_ts:
            idx = i
        else:
            break
    return idx


def nearest_idx_at_or_after(rows: List[Dict], target_ts: int) -> Optional[int]:
    for i, r in enumerate(rows):
        if r["ts"] >= target_ts:
            return i
    return None


def value_at_or_before(rows: List[Dict], target_ts: int, key: str, positive_only: bool = False) -> float:
    idx = nearest_idx_at_or_before(rows, target_ts)
    while idx is not None and idx >= 0:
        v = rows[idx][key]
        if is_valid(v, positive_only):
            return v
        idx -= 1
    return float("nan")


def value_at_or_after(rows: List[Dict], target_ts: int, key: str, positive_only: bool = False) -> float:
    idx = nearest_idx_at_or_after(rows, target_ts)
    while idx is not None and idx < len(rows):
        v = rows[idx][key]
        if is_valid(v, positive_only):
            return v
        idx += 1
    return float("nan")


def dew_point_c(temp_c: float, rh_pct: float) -> float:
    if not is_valid(temp_c) or not is_valid(rh_pct) or rh_pct <= 0.0:
        return float("nan")
    # Magnus formula approximation over typical ambient range.
    a = 17.625
    b = 243.04
    gamma = (a * temp_c) / (b + temp_c) + math.log(max(1e-6, rh_pct / 100.0))
    return (b * gamma) / (a - gamma)


def rolling_std_at_or_before(rows: List[Dict], target_ts: int, window_sec: int, key: str, positive_only: bool = False) -> float:
    vals: List[float] = []
    start_ts = target_ts - max(1, window_sec)
    for r in rows:
        ts = r["ts"]
        if ts < start_ts or ts > target_ts:
            continue
        v = r[key]
        if not is_valid(v, positive_only):
            continue
        vals.append(v)
    if len(vals) < 2:
        return 0.0
    mean = sum(vals) / len(vals)
    var = sum((v - mean) ** 2 for v in vals) / len(vals)
    return math.sqrt(var)


def classify_future(temp_now: float, hum_now: float, press_now: float, co2_now: float,
                    temp_future: float, hum_future: float, press_future: float, co2_future: float) -> str:
    temp_delta = temp_future - temp_now
    hum_delta = hum_future - hum_now
    press_delta = press_future - press_now

    if press_delta <= -2.2 and hum_delta >= 3.0:
        return "STORM RISK"
    if press_delta <= -1.8 and hum_delta >= 2.0:
        return "RAIN POSS"
    if press_delta >= 1.6 and hum_delta <= 1.0:
        return "CLEARING"
    if press_delta <= -1.2 and temp_delta <= -0.8:
        return "UNSETTLED"
    if temp_future >= 30.0 and hum_future >= 55.0:
        return "HEAT STRESS"
    if temp_future <= 8.0:
        return "COLD STRESS"
    if hum_future >= 95.0 and abs(temp_delta) <= 0.5:
        return "FOG RISK"
    if is_valid(co2_future, positive_only=True) and co2_future >= 1200.0:
        return "VENTILATE"
    if temp_delta >= 1.0:
        return "WARMING"
    if temp_delta <= -1.0:
        return "COOLING"
    if hum_delta >= 3.0:
        return "HUMIDIFYING"
    if hum_delta <= -3.0:
        return "DRYING"
    return "STEADY"


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--in-csv", required=True, help="Raw trend CSV from download_trend_log.py")
    p.add_argument("--out-csv", required=True, help="Output labeled feature CSV")
    p.add_argument("--horizon-min", type=int, default=180, help="Future horizon minutes for bootstrap label")
    args = p.parse_args()

    in_path = pathlib.Path(args.in_csv)
    out_path = pathlib.Path(args.out_csv)
    if not in_path.exists():
        print(f"Missing input CSV: {in_path}")
        return 2

    rows: List[Dict] = []
    with in_path.open("r", encoding="utf-8", newline="") as f:
        reader = csv.DictReader(f)
        for r in reader:
            ts = int(float(r.get("ts", "0") or 0))
            rows.append(
                {
                    "ts": ts,
                    "temp": parse_float(r.get("temp", "")),
                    "hum": parse_float(r.get("hum", "")),
                    "press": parse_float(r.get("press", "")),
                    "lux": parse_float(r.get("lux", "")),
                    "co2": parse_float(r.get("co2", "")),
                }
            )

    rows.sort(key=lambda x: x["ts"])
    rows = [r for r in rows if r["ts"] > 0]
    if len(rows) < 30:
        print("Need at least 30 rows in raw trend CSV.")
        return 2

    feature_fields = [
        # Keep first 10 in legacy order.
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
        "label",
    ]

    out_rows = []
    horizon_sec = max(30, args.horizon_min) * 60
    for cur in rows:
        ts = cur["ts"]
        t_now = cur["temp"]
        h_now = cur["hum"]
        p_now = cur["press"]
        c_now = cur["co2"]
        if not (is_valid(t_now) and is_valid(h_now) and is_valid(p_now, True)):
            continue

        t30 = value_at_or_before(rows, ts - 1800, "temp")
        t3h = value_at_or_before(rows, ts - 10800, "temp")
        t6h = value_at_or_before(rows, ts - 21600, "temp")
        t12h = value_at_or_before(rows, ts - 43200, "temp")
        h30 = value_at_or_before(rows, ts - 1800, "hum")
        h3h = value_at_or_before(rows, ts - 10800, "hum")
        h6h = value_at_or_before(rows, ts - 21600, "hum")
        p30 = value_at_or_before(rows, ts - 1800, "press", positive_only=True)
        p1h = value_at_or_before(rows, ts - 3600, "press", positive_only=True)
        p3h = value_at_or_before(rows, ts - 10800, "press", positive_only=True)
        p6h = value_at_or_before(rows, ts - 21600, "press", positive_only=True)
        p24h = value_at_or_before(rows, ts - 86400, "press", positive_only=True)
        c30 = value_at_or_before(rows, ts - 1800, "co2", positive_only=True)
        c3h = value_at_or_before(rows, ts - 10800, "co2", positive_only=True)
        l30 = value_at_or_before(rows, ts - 1800, "lux")

        fut_t = value_at_or_after(rows, ts + horizon_sec, "temp")
        fut_h = value_at_or_after(rows, ts + horizon_sec, "hum")
        fut_p = value_at_or_after(rows, ts + horizon_sec, "press", positive_only=True)
        fut_c = value_at_or_after(rows, ts + horizon_sec, "co2", positive_only=True)
        if not (is_valid(fut_t) and is_valid(fut_h) and is_valid(fut_p, True)):
            continue

        label = classify_future(t_now, h_now, p_now, c_now, fut_t, fut_h, fut_p, fut_c)
        if label not in CLASSES:
            continue

        minute_of_day = (ts % 86400) // 60
        angle = 2.0 * math.pi * (minute_of_day / 1440.0)
        hour_of_day = minute_of_day / 60.0
        day_night_flag = 1.0 if 7 <= hour_of_day < 19 else 0.0

        dp_now = dew_point_c(t_now, h_now)
        t3h_for_dp = value_at_or_before(rows, ts - 10800, "temp")
        h3h_for_dp = value_at_or_before(rows, ts - 10800, "hum")
        dp_3h = dew_point_c(t3h_for_dp, h3h_for_dp)
        press_std_3h = rolling_std_at_or_before(rows, ts, 10800, "press", positive_only=True)

        out_rows.append(
            {
                "temp_delta_30m": 0.0 if math.isnan(t30) else (t_now - t30),
                "temp_delta_3h": 0.0 if math.isnan(t3h) else (t_now - t3h),
                "temp_delta_6h": 0.0 if math.isnan(t6h) else (t_now - t6h),
                "temp_delta_12h": 0.0 if math.isnan(t12h) else (t_now - t12h),
                "hum_delta_30m": 0.0 if math.isnan(h30) else (h_now - h30),
                "hum_delta_3h": 0.0 if math.isnan(h3h) else (h_now - h3h),
                "hum_delta_6h": 0.0 if math.isnan(h6h) else (h_now - h6h),
                "press_delta_30m": 0.0 if math.isnan(p30) else (p_now - p30),
                "press_delta_1h": 0.0 if math.isnan(p1h) else (p_now - p1h),
                "press_delta_3h": 0.0 if math.isnan(p3h) else (p_now - p3h),
                "press_delta_6h": 0.0 if math.isnan(p6h) else (p_now - p6h),
                "press_delta_24h": 0.0 if math.isnan(p24h) else (p_now - p24h),
                "co2_delta_30m": 0.0 if (math.isnan(c_now) or math.isnan(c30)) else (c_now - c30),
                "co2_delta_3h": 0.0 if (math.isnan(c_now) or math.isnan(c3h)) else (c_now - c3h),
                "co2_level_now": 0.0 if math.isnan(c_now) else c_now,
                "dew_point_now": 0.0 if math.isnan(dp_now) else dp_now,
                "dew_point_delta_3h": 0.0 if (math.isnan(dp_now) or math.isnan(dp_3h)) else (dp_now - dp_3h),
                "lux_delta_30m": 0.0 if (math.isnan(cur["lux"]) or math.isnan(l30)) else (cur["lux"] - l30),
                "hour_sin": math.sin(angle),
                "hour_cos": math.cos(angle),
                "day_night_flag": day_night_flag,
                "rolling_std_press_3h": press_std_3h,
                "label": label,
            }
        )

    if len(out_rows) < 50:
        print(f"Only {len(out_rows)} rows generated; collect more data first.")
        return 2

    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=feature_fields)
        w.writeheader()
        for r in out_rows:
            w.writerow(r)

    print(f"Wrote dataset: {out_path}")
    print(f"Rows: {len(out_rows)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
