#ifndef ADAPTIVE_H
#define ADAPTIVE_H
#include <stdint.h>
typedef struct {double r,i;} AdComplex;
typedef struct {uint32_t frames,epoch,enabled,cal_state,cal_angle,cal_bins,accepted,rejected;
 uint32_t cal_power_bins,cal_coherent_bins,cal_amplitude_bins,cal_phase_bins;
 uint32_t cal_mode,cal_stage,path_bins,path_angle;} AdStatus;
typedef struct {double min_coherence,min_amplitude,max_amplitude,max_phase_deg;unsigned flags;} AdCalBin;
void ad_init(void);
void ad_observe(unsigned bin,const AdComplex x[8]);
void ad_frame_done(void);
int ad_calibrate(unsigned angle);
int ad_path_calibrate(unsigned angle); /* local acoustic path, not global microphone correction */
void ad_enable(unsigned enable);
void ad_status(AdStatus *out);
int ad_calibration_bin(unsigned bin,AdCalBin *out);
unsigned ad_epoch(void);
unsigned ad_freeze(void);
int ad_weights(double hz,double target,double wr[8],double wi[8]);
#endif
