#ifndef APP_STATE_H
#define APP_STATE_H
#include <stdint.h>

typedef struct {
    uint16_t target_angle_deg;
} AppState;

void app_state_init(void);
AppState app_state_get(void);
void app_state_set_angle(uint16_t angle_deg);

#endif
