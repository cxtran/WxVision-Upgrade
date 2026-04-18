#include "ml_predictor.h"

#include <float.h>
#include <math.h>
#include <time.h>
#include "ml_model_generated.h"

namespace
{
using wxv::ml::OutlookPrediction;
using wxv::ml::generated::kBias;
using wxv::ml::generated::kClassCount;
using wxv::ml::generated::kClassLabels;
using wxv::ml::generated::kFeatureCount;
using wxv::ml::generated::kFeatureMean;
using wxv::ml::generated::kFeatureScale;
using wxv::ml::generated::kHasModel;
using wxv::ml::generated::kWeights;

float getMostRecent(const SensorLogVector &log, float SensorSample::*field, bool positiveOnly = false)
{
    for (int i = static_cast<int>(log.size()) - 1; i >= 0; --i)
    {
        const float v = log[static_cast<size_t>(i)].*field;
        if (isnan(v))
            continue;
        if (positiveOnly && v <= 0.0f)
            continue;
        return v;
    }
    return NAN;
}

float getAtOrBefore(const SensorLogVector &log, uint32_t targetTs, float SensorSample::*field, bool positiveOnly = false)
{
    for (int i = static_cast<int>(log.size()) - 1; i >= 0; --i)
    {
        const SensorSample &s = log[static_cast<size_t>(i)];
        if (s.ts > targetTs)
            continue;
        const float v = s.*field;
        if (isnan(v))
            continue;
        if (positiveOnly && v <= 0.0f)
            continue;
        return v;
    }
    return NAN;
}

float dewPointC(float tempC, float humidityPct)
{
    if (isnan(tempC) || isnan(humidityPct) || humidityPct <= 0.0f)
        return NAN;
    const float a = 17.625f;
    const float b = 243.04f;
    const float rh = fmaxf(1e-6f, humidityPct / 100.0f);
    const float gamma = (a * tempC) / (b + tempC) + logf(rh);
    return (b * gamma) / (a - gamma);
}

float rollingStdAtOrBefore(const SensorLogVector &log, uint32_t targetTs, uint32_t windowSec, float SensorSample::*field, bool positiveOnly = false)
{
    if (log.empty())
        return 0.0f;
    const uint32_t startTs = (targetTs > windowSec) ? (targetTs - windowSec) : 0U;
    float mean = 0.0f;
    float m2 = 0.0f;
    int n = 0;
    for (size_t i = 0; i < log.size(); ++i)
    {
        const SensorSample &s = log[i];
        if (s.ts < startTs || s.ts > targetTs)
            continue;
        const float v = s.*field;
        if (isnan(v))
            continue;
        if (positiveOnly && v <= 0.0f)
            continue;
        ++n;
        const float delta = v - mean;
        mean += delta / static_cast<float>(n);
        const float delta2 = v - mean;
        m2 += delta * delta2;
    }
    if (n < 2)
        return 0.0f;
    return sqrtf(m2 / static_cast<float>(n));
}

void setFeature(float out[kFeatureCount], int idx, float value)
{
    if (idx >= 0 && idx < kFeatureCount)
        out[idx] = value;
}

bool buildFeatureVector(const SensorLogVector &log, float out[kFeatureCount])
{
    if (log.size() < 2)
        return false;

    const uint32_t nowTs = log.back().ts;
    if (nowTs == 0)
        return false;

    const float tNow = getMostRecent(log, &SensorSample::temp);
    const float hNow = getMostRecent(log, &SensorSample::hum);
    const float pNow = getMostRecent(log, &SensorSample::press, true);
    const float cNow = getMostRecent(log, &SensorSample::co2, true);
    if (isnan(tNow) || isnan(hNow) || isnan(pNow))
        return false;

    const float t30 = getAtOrBefore(log, (nowTs > 1800UL) ? (nowTs - 1800UL) : 0UL, &SensorSample::temp);
    const float t3h = getAtOrBefore(log, (nowTs > 10800UL) ? (nowTs - 10800UL) : 0UL, &SensorSample::temp);
    const float t6h = getAtOrBefore(log, (nowTs > 21600UL) ? (nowTs - 21600UL) : 0UL, &SensorSample::temp);
    const float t12h = getAtOrBefore(log, (nowTs > 43200UL) ? (nowTs - 43200UL) : 0UL, &SensorSample::temp);
    const float h30 = getAtOrBefore(log, (nowTs > 1800UL) ? (nowTs - 1800UL) : 0UL, &SensorSample::hum);
    const float h3h = getAtOrBefore(log, (nowTs > 10800UL) ? (nowTs - 10800UL) : 0UL, &SensorSample::hum);
    const float h6h = getAtOrBefore(log, (nowTs > 21600UL) ? (nowTs - 21600UL) : 0UL, &SensorSample::hum);
    const float p30 = getAtOrBefore(log, (nowTs > 1800UL) ? (nowTs - 1800UL) : 0UL, &SensorSample::press, true);
    const float p1h = getAtOrBefore(log, (nowTs > 3600UL) ? (nowTs - 3600UL) : 0UL, &SensorSample::press, true);
    const float p3h = getAtOrBefore(log, (nowTs > 10800UL) ? (nowTs - 10800UL) : 0UL, &SensorSample::press, true);
    const float p6h = getAtOrBefore(log, (nowTs > 21600UL) ? (nowTs - 21600UL) : 0UL, &SensorSample::press, true);
    const float p24h = getAtOrBefore(log, (nowTs > 86400UL) ? (nowTs - 86400UL) : 0UL, &SensorSample::press, true);
    const float c30 = getAtOrBefore(log, (nowTs > 1800UL) ? (nowTs - 1800UL) : 0UL, &SensorSample::co2, true);
    const float c3h = getAtOrBefore(log, (nowTs > 10800UL) ? (nowTs - 10800UL) : 0UL, &SensorSample::co2, true);
    const float lNow = getMostRecent(log, &SensorSample::lux);
    const float l30 = getAtOrBefore(log, (nowTs > 1800UL) ? (nowTs - 1800UL) : 0UL, &SensorSample::lux);

    for (int i = 0; i < kFeatureCount; ++i)
        out[i] = 0.0f;

    // First 10 features keep legacy order.
    setFeature(out, 0, isnan(t30) ? 0.0f : (tNow - t30));
    setFeature(out, 1, isnan(t3h) ? 0.0f : (tNow - t3h));
    setFeature(out, 2, isnan(h30) ? 0.0f : (hNow - h30));
    setFeature(out, 3, isnan(h3h) ? 0.0f : (hNow - h3h));
    setFeature(out, 4, isnan(p30) ? 0.0f : (pNow - p30));
    setFeature(out, 5, isnan(p3h) ? 0.0f : (pNow - p3h));
    setFeature(out, 6, isnan(p24h) ? 0.0f : (pNow - p24h));
    setFeature(out, 7, (isnan(cNow) || isnan(c3h)) ? 0.0f : (cNow - c3h));

    time_t raw = static_cast<time_t>(nowTs);
    tm *ti = localtime(&raw);
    const int minuteOfDay = ti ? (ti->tm_hour * 60 + ti->tm_min) : 0;
    const float angle = (2.0f * static_cast<float>(M_PI) * static_cast<float>(minuteOfDay)) / 1440.0f;
    setFeature(out, 8, sinf(angle));
    setFeature(out, 9, cosf(angle));

    const float dpNow = dewPointC(tNow, hNow);
    const float t3hForDp = t3h;
    const float h3hForDp = h3h;
    const float dp3h = dewPointC(t3hForDp, h3hForDp);
    const int hourOfDay = ti ? ti->tm_hour : 0;
    const float dayNightFlag = (hourOfDay >= 7 && hourOfDay < 19) ? 1.0f : 0.0f;
    const float pressStd3h = rollingStdAtOrBefore(log, nowTs, 10800U, &SensorSample::press, true);

    // Extended features appended after legacy set.
    setFeature(out, 10, isnan(t6h) ? 0.0f : (tNow - t6h));
    setFeature(out, 11, isnan(t12h) ? 0.0f : (tNow - t12h));
    setFeature(out, 12, isnan(h6h) ? 0.0f : (hNow - h6h));
    setFeature(out, 13, isnan(p1h) ? 0.0f : (pNow - p1h));
    setFeature(out, 14, isnan(p6h) ? 0.0f : (pNow - p6h));
    setFeature(out, 15, (isnan(cNow) || isnan(c30)) ? 0.0f : (cNow - c30));
    setFeature(out, 16, isnan(cNow) ? 0.0f : cNow);
    setFeature(out, 17, isnan(dpNow) ? 0.0f : dpNow);
    setFeature(out, 18, (isnan(dpNow) || isnan(dp3h)) ? 0.0f : (dpNow - dp3h));
    setFeature(out, 19, (isnan(lNow) || isnan(l30)) ? 0.0f : (lNow - l30));
    setFeature(out, 20, dayNightFlag);
    setFeature(out, 21, pressStd3h);
    return true;
}
} // namespace

namespace wxv::ml
{
OutlookPrediction predictOutlookFromLog(const SensorLogVector &log)
{
    OutlookPrediction out;
    if (!kHasModel || kFeatureCount <= 0 || kClassCount <= 0)
        return out;

    float x[kFeatureCount];
    if (!buildFeatureVector(log, x))
        return out;

    float z[kClassCount];
    float zMax = -FLT_MAX;
    for (int c = 0; c < kClassCount; ++c)
    {
        float score = kBias[c];
        for (int i = 0; i < kFeatureCount; ++i)
        {
            const float denom = (fabsf(kFeatureScale[i]) < 1e-6f) ? 1.0f : kFeatureScale[i];
            const float xn = (x[i] - kFeatureMean[i]) / denom;
            score += kWeights[c][i] * xn;
        }
        z[c] = score;
        if (score > zMax)
            zMax = score;
    }

    float sumExp = 0.0f;
    int bestIdx = 0;
    float bestProb = -1.0f;
    float probs[kClassCount];
    for (int c = 0; c < kClassCount; ++c)
    {
        probs[c] = expf(z[c] - zMax);
        sumExp += probs[c];
    }
    if (sumExp <= 0.0f)
        return out;

    for (int c = 0; c < kClassCount; ++c)
    {
        probs[c] /= sumExp;
        if (probs[c] > bestProb)
        {
            bestProb = probs[c];
            bestIdx = c;
        }
    }

    out.available = true;
    out.label = kClassLabels[bestIdx];
    const int pct = static_cast<int>(lroundf(bestProb * 100.0f));
    out.confidencePct = static_cast<uint8_t>((pct < 0) ? 0 : (pct > 100 ? 100 : pct));
    return out;
}
} // namespace wxv::ml
