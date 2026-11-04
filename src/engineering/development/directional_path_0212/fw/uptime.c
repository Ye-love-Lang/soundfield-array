#include "uptime.h"

#ifdef HOST_TEST
static uint32_t fake_ms = 1234U;
void uptime_init(void) { fake_ms = 1234U; }
uint32_t uptime_millis(void) { return fake_ms; }
void uptime_test_set(uint32_t value) { fake_ms = value; }
#else
#include "xil_io.h"
#include "xparameters.h"
#include "sleep.h"

#ifndef XPAR_GLOBAL_TMR_BASEADDR
#error "Zynq-7000 Global Timer macro missing; check Standalone BSP"
#endif
#ifndef XPAR_CPU_CORE_CLOCK_FREQ_HZ
#error "Cortex-A9 CPU clock macro missing; check Standalone BSP"
#endif

#define GT_BASE ((UINTPTR)XPAR_GLOBAL_TMR_BASEADDR)
#define GT_LO   0x00U
#define GT_HI   0x04U
#define GT_CTRL 0x08U

static uint64_t g_start_ticks;

static uint64_t read_global_timer(void)
{
    uint32_t high1, high2, low;
    do {
        high1 = Xil_In32(GT_BASE + GT_HI);
        low = Xil_In32(GT_BASE + GT_LO);
        high2 = Xil_In32(GT_BASE + GT_HI);
    } while (high1 != high2);
    return ((uint64_t)high1 << 32) | low;
}

void uptime_init(void)
{
    /* Start BSP's lazy sleep timer before saving the uptime epoch. */
    usleep(1U);
    uint32_t ctl = Xil_In32(GT_BASE + GT_CTRL);
    if ((ctl & 1U) == 0U) {
        Xil_Out32(GT_BASE + GT_CTRL, ctl | 1U);
    }
    g_start_ticks = read_global_timer();
}

uint32_t uptime_millis(void)
{
    const uint64_t ticks_per_sec =
        ((uint64_t)XPAR_CPU_CORE_CLOCK_FREQ_HZ) / 2ULL;
    uint64_t elapsed = read_global_timer() - g_start_ticks;
    /* 64-bit seconds + remaining ticks avoids overflow in elapsed * 1000. */
    uint64_t ms = (elapsed / ticks_per_sec) * 1000ULL +
                  ((elapsed % ticks_per_sec) * 1000ULL) / ticks_per_sec;
    return (uint32_t)ms;
}
#endif
