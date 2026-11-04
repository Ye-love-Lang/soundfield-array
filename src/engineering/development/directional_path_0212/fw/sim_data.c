#include "sim_data.h"

static SimSnapshot g_override;
static int g_override_active;

#define DEFAULT_SOURCE_COUNT 2U
#define PEAK_SEPARATION_DEG 15U
#define PROMINENCE_WINDOW_DEG 20U

/* Triangle wave oscillation, expressed in integer steps of 1 second. */
static int triangle(uint32_t time_sec, int amplitude, uint32_t half_period)
{
    const uint32_t period = half_period * 2U;
    uint32_t phase = time_sec % period;
    if (phase <= half_period)
        return -amplitude + (int)((2U * (uint32_t)amplitude * phase) / half_period);
    return amplitude - (int)((2U * (uint32_t)amplitude * (phase - half_period)) / half_period);
}

static int circular_distance(int a, int b)
{
    int difference = a > b ? a - b : b - a;
    return difference <= 180 ? difference : 360 - difference;
}

static uint16_t clamp_energy(int value)
{
    if (value < 0) return 0U;
    if (value > 1000) return 1000U;
    return (uint16_t)value;
}

void sim_data_snapshot(uint32_t uptime_ms, SimSnapshot *out)
{
    if (g_override_active) {
        *out = g_override;
        return;
    }
    uint32_t sec = uptime_ms / 1000U;
    int theta0 = 45 + triangle(sec, 15, 15U);
    int theta1 = 220 + triangle(sec + 7U, 22, 22U);
    int level0 = 820 + triangle(sec + 9U, 55, 11U);
    int level1 = 625 + triangle(sec + 4U, 45, 14U);

    out->sources[0].source_id = 0U;
    out->sources[0].angle_deg = (uint16_t)theta0;
    out->sources[0].strength = (uint16_t)level0;
    out->sources[1].source_id = 1U;
    out->sources[1].angle_deg = (uint16_t)theta1;
    out->sources[1].strength = (uint16_t)level1;
    out->source_count = DEFAULT_SOURCE_COUNT;

    for (unsigned i = 0U; i < SIM_SPECTRUM_POINTS; ++i) {
        int angle = (int)i * 5;
        int d0 = circular_distance(angle, theta0);
        int d1 = circular_distance(angle, theta1);
        int energy = 36 + (int)((i * 17U + (sec % 97U) * 5U) % 25U);
        /* Smooth, deterministic, circular 30-degree-wide simulated peaks. */
        if (d0 < 30) {
            int x = 30 - d0;
            energy += level0 * x * x / 900;
        }
        if (d1 < 30) {
            int x = 30 - d1;
            energy += level1 * x * x / 900;
        }
        out->spectrum[i] = clamp_energy(energy);
    }
}

void sim_data_make_spectrum_from_sources(SimSnapshot *snapshot)
{
    for (unsigned i = 0U; i < SIM_SPECTRUM_POINTS; ++i) {
        int angle = (int)i * 5;
        int energy = 0;
        for (unsigned j = 0U; j < snapshot->source_count; ++j) {
            int distance = circular_distance(angle,
                (int)snapshot->sources[j].angle_deg);
            if (distance < 30) {
                int x = 30 - distance;
                energy += (int)snapshot->sources[j].strength * x * x / 900;
            }
        }
        snapshot->spectrum[i] = clamp_energy(energy);
    }
}

/* Circular five-tap smoothing suppresses isolated one-degree noise. */
static uint16_t smoothed_energy(const uint16_t energy[SIM_ENERGY_POINTS],
                                unsigned angle)
{
    unsigned before2 = (angle + SIM_ENERGY_POINTS - 2U) % SIM_ENERGY_POINTS;
    unsigned before1 = (angle + SIM_ENERGY_POINTS - 1U) % SIM_ENERGY_POINTS;
    unsigned after1 = (angle + 1U) % SIM_ENERGY_POINTS;
    unsigned after2 = (angle + 2U) % SIM_ENERGY_POINTS;
    unsigned weighted = energy[before2] + 2U * energy[before1] +
                        3U * energy[angle] + 2U * energy[after1] +
                        energy[after2];
    return (uint16_t)((weighted + 4U) / 9U);
}

unsigned sim_data_set_energy_360(const uint16_t energy[SIM_ENERGY_POINTS])
{
    uint16_t smooth[SIM_ENERGY_POINTS];
    SimSnapshot snapshot = {0};
    unsigned global_peak = 0U;

    for (unsigned i = 0U; i < SIM_ENERGY_POINTS; ++i) {
        smooth[i] = smoothed_energy(energy, i);
        if (smooth[i] > global_peak) global_peak = smooth[i];
        if (energy[i] > snapshot.spectrum[i / 5U])
            snapshot.spectrum[i / 5U] = energy[i];
    }

    /* Select up to four distinct circular local maxima, strongest first. */
    for (unsigned rank = 0U; rank < SIM_SOURCE_COUNT; ++rank) {
        int best_angle = -1;
        unsigned best_level = 0U;
        unsigned min_level = global_peak / 5U;
        unsigned min_prominence = global_peak / 10U;
        if (min_level < 80U) min_level = 80U;
        if (min_prominence < 40U) min_prominence = 40U;

        for (unsigned angle = 0U; angle < SIM_ENERGY_POINTS; ++angle) {
            unsigned left = (angle + SIM_ENERGY_POINTS - 1U) % SIM_ENERGY_POINTS;
            unsigned right = (angle + 1U) % SIM_ENERGY_POINTS;
            unsigned min_left = smooth[angle], min_right = smooth[angle];
            int too_close = 0;
            if (smooth[angle] < min_level ||
                smooth[angle] <= smooth[left] || smooth[angle] < smooth[right])
                continue;
            for (unsigned chosen = 0U; chosen < snapshot.source_count; ++chosen) {
                if ((unsigned)circular_distance((int)angle,
                    (int)snapshot.sources[chosen].angle_deg) < PEAK_SEPARATION_DEG) {
                    too_close = 1;
                    break;
                }
            }
            if (too_close) continue;
            for (unsigned offset = 1U; offset <= PROMINENCE_WINDOW_DEG; ++offset) {
                unsigned li = (angle + SIM_ENERGY_POINTS - offset) % SIM_ENERGY_POINTS;
                unsigned ri = (angle + offset) % SIM_ENERGY_POINTS;
                if (smooth[li] < min_left) min_left = smooth[li];
                if (smooth[ri] < min_right) min_right = smooth[ri];
            }
            {
                unsigned higher_side = min_left > min_right ? min_left : min_right;
                if (smooth[angle] - higher_side < min_prominence) continue;
            }
            if (best_angle < 0 || smooth[angle] > best_level) {
                best_angle = (int)angle;
                best_level = smooth[angle];
            }
        }
        if (best_angle < 0) break;
        snapshot.sources[rank].source_id = rank;
        snapshot.sources[rank].angle_deg = (uint16_t)best_angle;
        snapshot.sources[rank].strength = energy[best_angle];
        ++snapshot.source_count;
    }

    sim_data_set_override(&snapshot);
    return snapshot.source_count;
}

void sim_data_set_override(const SimSnapshot *snapshot)
{
    g_override = *snapshot;
    g_override_active = 1;
}

void sim_data_clear_override(void)
{
    g_override_active = 0;
}
