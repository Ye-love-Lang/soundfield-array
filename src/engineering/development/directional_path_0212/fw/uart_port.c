#include "uart_port.h"
#include "xparameters.h"
#include "xuartps.h"
#include <stddef.h>

#define UART_BAUDRATE 115200U
#define UART1_BASE_EXPECTED ((UINTPTR)0xE0001000U)
static XUartPs g_uart;

int uart_port_init(void)
{
    XUartPs_Config *config;
    int status;
#ifdef SDT
    config = XUartPs_LookupConfig((UINTPTR)XPAR_XUARTPS_0_BASEADDR);
#else
    config = XUartPs_LookupConfig(XPAR_XUARTPS_0_DEVICE_ID);
#endif
    if (config == NULL || config->BaseAddress != UART1_BASE_EXPECTED) return XST_FAILURE;
    status = XUartPs_CfgInitialize(&g_uart, config, config->BaseAddress);
    if (status != XST_SUCCESS) return status;
    status = XUartPs_SetBaudRate(&g_uart, UART_BAUDRATE);
    if (status != XST_SUCCESS) return status;
    XUartPs_SetOperMode(&g_uart, XUARTPS_OPER_MODE_NORMAL);
    return XST_SUCCESS;
}

int uart_port_try_read(u8 *value)
{
    if (value == NULL) return 0;
    return XUartPs_Recv(&g_uart, value, 1U) == 1U;
}

int uart_port_try_write(u8 value)
{
    /* Copies into TX FIFO when there is space; never waits for a TX slot. */
    return XUartPs_Send(&g_uart, &value, 1U) == 1U;
}

void uart_port_write_byte(u8 value)
{
    while (!uart_port_try_write(value)) { }
}

void uart_port_write_string(const char *str)
{
    if (!str) return;
    while (*str) uart_port_write_byte((u8)*str++);
}
