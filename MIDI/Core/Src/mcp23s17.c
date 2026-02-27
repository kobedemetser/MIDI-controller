#include "mcp23s17.h"

// External handle from main.c (Now using SPI1 as per D10-D13 wiring)
extern SPI_HandleTypeDef hspi1;

// Default CS from project config (can be overridden in main.h)
#ifndef MCP_CS_GPIO_Port
#define MCP_CS_GPIO_Port GPIOA
#endif
#ifndef MCP_CS_Pin
#define MCP_CS_Pin GPIO_PIN_4
#endif

typedef struct
{
    GPIO_TypeDef *port;
    uint16_t pin;
} mcp_cs_t;

static mcp_cs_t s_active_cs = {MCP_CS_GPIO_Port, MCP_CS_Pin};
static uint8_t s_mcp_ready = 0;

static void mcp_cs_write(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state)
{
    HAL_GPIO_WritePin(port, pin, state);
}

static HAL_StatusTypeDef mcp_write_cs(GPIO_TypeDef *port, uint16_t pin, uint8_t reg, uint8_t value)
{
    uint8_t data[3];
    data[0] = MCP_OPCODE_WRITE;
    data[1] = reg;
    data[2] = value;

    mcp_cs_write(port, pin, GPIO_PIN_RESET);
    HAL_StatusTypeDef st = HAL_SPI_Transmit(&hspi1, data, 3, 10);
    mcp_cs_write(port, pin, GPIO_PIN_SET);
    return st;
}

static HAL_StatusTypeDef mcp_read_cs(GPIO_TypeDef *port, uint16_t pin, uint8_t reg, uint8_t *value)
{
    uint8_t tx_data[3];
    uint8_t rx_data[3];

    tx_data[0] = MCP_OPCODE_READ;
    tx_data[1] = reg;
    tx_data[2] = 0x00;

    mcp_cs_write(port, pin, GPIO_PIN_RESET);
    HAL_StatusTypeDef st = HAL_SPI_TransmitReceive(&hspi1, tx_data, rx_data, 3, 10);
    mcp_cs_write(port, pin, GPIO_PIN_SET);

    if (st != HAL_OK)
    {
        return st;
    }

    *value = rx_data[2];
    return HAL_OK;
}

static uint8_t mcp_probe_cs(GPIO_TypeDef *port, uint16_t pin)
{
    uint8_t read_back = 0;

    // Quick sanity probe: write/read same register with two patterns.
    if (mcp_write_cs(port, pin, MCP_IODIRA, 0xA5) != HAL_OK)
    {
        return 0;
    }
    if ((mcp_read_cs(port, pin, MCP_IODIRA, &read_back) != HAL_OK) || (read_back != 0xA5))
    {
        return 0;
    }

    if (mcp_write_cs(port, pin, MCP_IODIRA, 0x5A) != HAL_OK)
    {
        return 0;
    }
    if ((mcp_read_cs(port, pin, MCP_IODIRA, &read_back) != HAL_OK) || (read_back != 0x5A))
    {
        return 0;
    }

    return 1;
}

/**
  * @brief  Write a value to a register of MCP23S17
  * @param  reg: Register address
  * @param  value: Data to write
  * @retval None
  */
void MCP_Write(uint8_t reg, uint8_t value) {
    (void)mcp_write_cs(s_active_cs.port, s_active_cs.pin, reg, value);
}

/**
  * @brief  Read a value from a register of MCP23S17
  * @param  reg: Register address
  * @retval value: Data read
  */
uint8_t MCP_Read(uint8_t reg) {
    uint8_t value = 0;
    if (mcp_read_cs(s_active_cs.port, s_active_cs.pin, reg, &value) != HAL_OK)
    {
        return 0;
    }
    return value;
}

/**
  * @brief  Initialize the MCP23S17
  * @retval None
  */
void MCP_Init(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    const mcp_cs_t candidates[] = {
        {MCP_CS_GPIO_Port, MCP_CS_Pin},  // Project-configured default first
        {GPIOC, GPIO_PIN_9},             // Arduino D10 on NUCLEO-H533RE
        {GPIOA, GPIO_PIN_4},             // Legacy project wiring
        {GPIOB, GPIO_PIN_6}              // Alternative wiring found on some setups
    };

    s_mcp_ready = 0;

    // Ensure GPIO clocks for candidate CS pins are enabled
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    // Configure candidates as outputs, idle HIGH
    for (uint8_t i = 0; i < (sizeof(candidates) / sizeof(candidates[0])); i++)
    {
        GPIO_InitStruct.Pin = candidates[i].pin;
        GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
        HAL_GPIO_Init(candidates[i].port, &GPIO_InitStruct);
        mcp_cs_write(candidates[i].port, candidates[i].pin, GPIO_PIN_SET);
    }

    // Probe candidate CS pins and pick the first that replies correctly
    for (uint8_t i = 0; i < (sizeof(candidates) / sizeof(candidates[0])); i++)
    {
        if (mcp_probe_cs(candidates[i].port, candidates[i].pin))
        {
            s_active_cs = candidates[i];
            s_mcp_ready = 1;
            break;
        }
    }

    if (s_mcp_ready == 0)
    {
        return;
    }

    // 1) GPA0..GPA3 outputs (columns), GPA4..GPA7 inputs
    MCP_Write(MCP_IODIRA, 0xF0);

    // 2) GPB0..GPB3 inputs (rows), GPB4..GPB7 don't care
    MCP_Write(MCP_IODIRB, 0x0F);

    // 3) Pull-ups on row inputs
    MCP_Write(MCP_GPPUB, 0x0F);

    // 4) Idle columns HIGH
    MCP_Write(MCP_GPIOA, 0x0F);
}

uint8_t MCP_IsReady(void)
{
    return s_mcp_ready;
}

uint8_t MCP_TestLink(void)
{
    if (s_mcp_ready == 0)
    {
        return 0;
    }

    if (!mcp_probe_cs(s_active_cs.port, s_active_cs.pin))
    {
        s_mcp_ready = 0;
        return 0;
    }

    // Restore runtime matrix configuration after probe writes.
    MCP_Write(MCP_IODIRA, 0xF0);
    MCP_Write(MCP_IODIRB, 0x0F);
    MCP_Write(MCP_GPPUB, 0x0F);
    MCP_Write(MCP_GPIOA, 0x0F);

    return 1;
}
