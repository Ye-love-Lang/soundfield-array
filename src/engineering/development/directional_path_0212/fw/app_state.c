#include "app_state.h"

static AppState g_state;

void app_state_init(void)
{
    g_state.target_angle_deg = 0U;
}

AppState app_state_get(void)
{
    return g_state;
}

void app_state_set_angle(uint16_t angle_deg)
{
    g_state.target_angle_deg = angle_deg;
}
