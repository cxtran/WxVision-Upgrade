#!/usr/bin/env python3
"""
Download WxVision trend log from /trend.json and store as JSON/CSV for offline ML.

Example:
  python tools/ml/download_trend_log.py --host http://visionwx.local --out-dir tools/ml/data
"""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import json
import pathlib
import sys
import urllib.request
import time


def to_float(value):
    if value is None:
        return float("nan")
    try:
        return float(value)
    except Exception:
        return float("nan")


def parse_array_payload(payload: str):
    payload = payload.strip().replace("\x00", "")
    # First try direct parse.
    try:
        obj = json.loads(payload)
        if isinstance(obj, list):
            return obj
    except Exception:
        pass

    # Fallback: extract the largest [...] block.
    start = payload.find("[")
    end = payload.rfind("]")
    if start >= 0 and end > start:
        sliced = payload[start : end + 1]
        obj = json.loads(sliced)
        if isinstance(obj, list):
            return obj

    # Recovery for truncated payloads:
    # keep only complete objects up to the last "}," boundary, then close array.
    if start >= 0:
        body = payload[start + 1 :]
        last_obj_sep = body.rfind("},")
        if last_obj_sep > 0:
            repaired = "[" + body[: last_obj_sep + 1] + "]"
            obj = json.loads(repaired)
            if isinstance(obj, list):
                return obj
    raise ValueError("payload is not a valid JSON array")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True, help="Device base URL, e.g. http://visionwx.local")
    parser.add_argument("--out-dir", default="tools/ml/data", help="Output folder")
    parser.add_argument("--timeout", type=float, default=12.0, help="HTTP timeout seconds")
    parser.add_argument("--retries", type=int, default=3, help="Retry count on parse/fetch failure")
    parser.add_argument("--limit", type=int, default=0, help="Optional trend row limit (uses /trend.json?limit=N)")
    args = parser.parse_args()

    base = args.host.rstrip("/")
    url = f"{base}/trend.json"
    if args.limit and args.limit > 0:
        url += f"?limit={int(args.limit)}"
    out_dir = pathlib.Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    timestamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
    json_path = out_dir / f"trend_{timestamp}.json"
    csv_path = out_dir / f"trend_{timestamp}.csv"

    payload = ""
    data = None
    last_err = None
    for attempt in range(1, max(1, args.retries) + 1):
        try:
            with urllib.request.urlopen(url, timeout=args.timeout) as resp:
                payload = resp.read().decode("utf-8", errors="replace")
            data = parse_array_payload(payload)
            break
        except Exception as exc:
            last_err = exc
            print(f"Attempt {attempt}/{args.retries} failed: {exc}")
            if payload:
                bad_path = out_dir / f"trend_bad_attempt{attempt}_{timestamp}.txt"
                bad_path.write_text(payload, encoding="utf-8", errors="replace")
                print(f"Saved bad payload for inspection: {bad_path}")
            if attempt < args.retries:
                time.sleep(0.8)

    if data is None:
        print(f"Failed to fetch/parse {url}: {last_err}")
        return 2

    json_path.write_text(payload, encoding="utf-8")

    fields = ["ts", "temp", "hum", "press", "lux", "co2"]
    with csv_path.open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        for row in data:
            if not isinstance(row, dict):
                continue
            w.writerow(
                {
                    "ts": int(row.get("ts", 0) or 0),
                    "temp": to_float(row.get("temp")),
                    "hum": to_float(row.get("hum")),
                    "press": to_float(row.get("press")),
                    "lux": to_float(row.get("lux")),
                    "co2": to_float(row.get("co2")),
                }
            )

    print(f"Saved JSON: {json_path}")
    print(f"Saved CSV : {csv_path}")
    print(f"Rows      : {len(data)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
