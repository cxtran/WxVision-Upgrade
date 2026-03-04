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

By default, this pipeline removes older `trend_*.csv/json` files after a
successful run and keeps only the input pair used for training.
To keep all historical downloads:

```bash
python tools/ml/run_training_pipeline.py --data-dir tools/ml/data --keep-old-data
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
