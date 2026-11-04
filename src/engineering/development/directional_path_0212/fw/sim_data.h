#ifndef SIM_DATA_H
#define SIM_DATA_H
#include <stdint.h>

#define SIM_SPECTRUM_POINTS 72U
#define SIM_ENERGY_POINTS 360U
#define SIM_SOURCE_COUNT 4U
#define SIM_SAMPLE_RATE_HZ 48000U

typedef struct {
    uint32_t source_id;
    uint16_t angle_deg;
    uint16_t strength;
} SimSource;

typedef struct {
    uint16_t spectrum[SIM_SPECTRUM_POINTS];
    SimSource sources[SIM_SOURCE_COUNT];
    uint8_t source_count;
} SimSnapshot;

/* Deterministic pure simulation: no ADC, PL, mic or actual recording involved. */
void sim_data_snapshot(uint32_t uptime_ms, SimSnapshot *out);
void sim_data_set_override(const SimSnapshot *snapshot);
void sim_data_clear_override(void);
void sim_data_make_spectrum_from_sources(SimSnapshot *snapshot);
/* Convert 360 simulated 1-degree energies into display bins and detected peaks. */
unsigned sim_data_set_energy_360(const uint16_t energy[SIM_ENERGY_POINTS]);

#endif
