#pragma once
#include <stdint.h>
#define KWS_FEATURE_DIM 8
#define KWS_N_FRAMES 40
#define KWS_WIN 400
#define KWS_HOP 160
extern const float kws_mu[KWS_FEATURE_DIM];
extern const float kws_sd_inv[KWS_FEATURE_DIM];
extern const int8_t kws_w_q[KWS_FEATURE_DIM];
extern const float kws_w_scale;
extern const float kws_b;
