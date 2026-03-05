# ML Starter For WxVision

This folder provides a starter flow to train a lightweight weather-outlook model
and export it to firmware.

## 1) Download Raw Log From Device

On your PC (same network as device):

```bash
python tools/ml/download_trend_log.py --host http://visionwx.local --out-dir tools/ml/data
```

Or use device IP:

```bash
python tools/ml/download_trend_log.py --host http://192.168.1.123 --out-dir tools/ml/data
```

Request a specific number of rows (after firmware update):

```bash
python tools/ml/download_trend_log.py --host http://192.168.1.123 --out-dir tools/ml/data --limit 120
```

This saves:

- `trend_YYYYMMDD_HHMMSS.json`
- `trend_YYYYMMDD_HHMMSS.csv`

## 2) Build Labeled Training Dataset (Offline Bootstrap)

Convert raw trend CSV into model features + labels:

```bash
python tools/ml/build_outlook_dataset.py \
  --in-csv tools/ml/data/trend_YYYYMMDD_HHMMSS.csv \
  --out-csv tools/ml/data/outlook_dataset.csv \
  --horizon-min 180
```

Output columns:

Legacy-compatible core (first 10):
- `temp_delta_30m`
- `temp_delta_3h`
- `hum_delta_30m`
- `hum_delta_3h`
- `press_delta_30m`
- `press_delta_3h`
- `press_delta_24h`
- `co2_delta_3h`
- `hour_sin`
- `hour_cos`

Extended:
- `temp_delta_6h`
- `temp_delta_12h`
- `hum_delta_6h`
- `press_delta_1h`
- `press_delta_6h`
- `co2_delta_30m`
- `co2_level_now`
- `dew_point_now`
- `dew_point_delta_3h`
- `lux_delta_30m`
- `day_night_flag`
- `rolling_std_press_3h`
- `label` (`RAIN POSS`, `CLEARING`, `UNSETTLED`, `STEADY`, `WARMING`, `COOLING`, `HUMIDIFYING`, `DRYING`, `STORM RISK`, `FOG RISK`, `VENTILATE`, `HEAT STRESS`, `COLD STRESS`)

Current training model supports 13 classes. User meaning + technical trigger:

- `STORM RISK`: Weather may turn severe soon. Trigger: pressure drops hard (`<= -2.2`) and humidity rises (`>= +3`).
- `RAIN POSS`: Rain is becoming more likely. Trigger: pressure drops (`<= -1.8`) and humidity rises (`>= +2`).
- `CLEARING`: Conditions are improving; skies may open up. Trigger: pressure rises (`>= +1.6`) and humidity stays low-change (`<= +1`).
- `UNSETTLED`: Weather is becoming less stable. Trigger: pressure drops (`<= -1.2`) and temperature cools (`<= -0.8`).
- `HEAT STRESS`: Hot and humid conditions may feel uncomfortable or risky. Trigger: future temp `>= 30C` and humidity `>= 55%`.
- `COLD STRESS`: Temperature may become unusually cold. Trigger: future temp `<= 8C`.
- `FOG RISK`: Fog is possible. Trigger: future humidity `>= 95%` with near-flat temp change (`|delta temp| <= 0.5`).
- `VENTILATE`: Ventilation is recommended due to stale-air risk. Trigger: future CO2 `>= 1200 ppm`.
- `WARMING`: Temperature is trending warmer. Trigger: temp rise `>= +1.0`.
- `COOLING`: Temperature is trending cooler. Trigger: temp drop `<= -1.0`.
- `HUMIDIFYING`: Humidity is increasing. Trigger: humidity rise `>= +3`.
- `DRYING`: Humidity is decreasing. Trigger: humidity drop `<= -3`.
- `STEADY`: Conditions are relatively stable. Trigger: fallback when none of the above triggers.

Important: these are all available classes, but during training some may be auto-disabled by `--min-class-support` if there is not enough data for that class.

## 2) Train + Export

```bash
python tools/ml/train_outlook_model.py --csv tools/ml/data/outlook_dataset.csv --out include/ml_model_generated.h
```

## One-Command Pipeline

Run dataset build + training together:

```bash
python tools/ml/run_training_pipeline.py --in-csv tools/ml/data/trend_YYYYMMDD_HHMMSS.csv
```

Or auto-pick the newest `trend_*.csv`:

```bash
python tools/ml/run_training_pipeline.py --data-dir tools/ml/data
```

All available switches:

- `-h`, `--help`: Show help.
- `--in-csv <path>`: Raw trend CSV input. If omitted, newest `trend_*.csv` in `--data-dir` is used.
- `--data-dir <dir>`: Directory containing downloaded trend files. Default: `tools/ml/data`.
- `--dataset-csv <path>`: Output labeled dataset CSV. Default: `tools/ml/data/outlook_dataset.csv`.
- `--out-header <path>`: Output firmware header. Default: `include/ml_model_generated.h`.
- `--metadata-out <path>`: Output training metadata JSON. Default: `tools/ml/data/model_metadata.json`.
- `--horizon-min <int>`: Future horizon (minutes) used to build labels. Default: `180`.
- `--min-class-support <int>`: Minimum rows for a class to remain enabled in exported model. Default: `30`.
- `--min-enabled-classes <int>`: Fail if enabled classes are fewer than this threshold. Default: `2`.
- `--min-accuracy <float>`: Fail if test accuracy is below this value (`0..1`). Default: `0.0` (disabled).
- `--min-weighted-f1 <float>`: Fail if weighted F1 is below this value (`0..1`). Default: `0.0` (disabled).
- `--cleanup-old-data`: After successful training, delete older `trend_*.csv/json` in `--data-dir` and keep only the input pair. Default: disabled.
- `--keep-old-data`: Explicitly keep all downloaded `trend_*.csv/json` files (overrides cleanup if both are passed).

By default, this pipeline keeps all downloaded `trend_*.csv/json` files.
To clean up older files after a successful run and keep only the input pair used for training:

```bash
python tools/ml/run_training_pipeline.py --data-dir tools/ml/data --cleanup-old-data
```

Use class-support gating (recommended) so low-data classes stay disabled:

```bash
python tools/ml/run_training_pipeline.py --data-dir tools/ml/data --min-class-support 30 --min-enabled-classes 2
```

Training metadata is written to `tools/ml/data/model_metadata.json` by default.

For release builds, you can also enforce minimum quality metrics:

```bash
python tools/ml/run_training_pipeline.py --data-dir tools/ml/data --min-class-support 25 --min-enabled-classes 2 --min-accuracy 0.70 --min-weighted-f1 0.75
```

Install dependencies if needed:

```bash
pip install -r tools/ml/requirements.txt
```

## 3) Build Firmware

```bash
pio run -e esp32dev
```

If a trained model is exported, prediction pages can show an extra `ML OUTLOOK`
page. Without a model, the firmware keeps current rule-based behavior.
