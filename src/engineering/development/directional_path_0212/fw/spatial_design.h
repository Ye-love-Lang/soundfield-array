#ifndef SPATIAL_DESIGN_H
#define SPATIAL_DESIGN_H
#include <stdint.h>
#define SF_TAPS 512U
#define SF_COEFFICIENTS 4096U
/* Incremental, main-loop-only design. No recursion, allocation or audio blocking. */
int sf_design_begin(unsigned target,int interference,unsigned dc_cut_hz);
int sf_design_step(void); /* -1 failed,0 busy,1 ready; one bin or channel per call */
int32_t sf_design_coefficient(unsigned index);
unsigned sf_design_error(void); /* 1 weight,2 spectrum,3 quantization,4 DC correction */
int sf_weights(double hz,double target,int interference,double wr[8],double wi[8]);
#endif
