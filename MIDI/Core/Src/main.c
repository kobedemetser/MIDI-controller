/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Main program body
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "tusb.h"
#include "mcp23s17.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define MATRIX_ROWS 4u
#define MATRIX_COLS 4u
#define MATRIX_KEYS (MATRIX_ROWS * MATRIX_COLS)
#define MATRIX_ROW_MASK ((uint8_t)((1u << MATRIX_ROWS) - 1u))
#define MATRIX_COLUMN_MASK ((uint8_t)((1u << MATRIX_COLS) - 1u))
#define MATRIX_PORTA_UNUSED_INPUT_MASK 0xF0u
#define MATRIX_ALL_COLUMNS_INPUT_IODIR (MATRIX_PORTA_UNUSED_INPUT_MASK | MATRIX_COLUMN_MASK)
#define MATRIX_SCAN_MS 2u
#define MATRIX_DEBOUNCE_SCANS 8u
#define MATRIX_IDLE_RAINBOW_DELAY_MS 3000u
#define MATRIX_IDLE_RAINBOW_FRAME_MS 50u
#define MIDI_BASE_NOTE 60u
#define MIDI_NOTE_VELOCITY 127u
#define NUM_POTS 8u
/* ADC ranks are configured as IN0, IN1, IN5, IN8, IN9, IN10, IN11, IN12. */
#define POT_PROCESS_MS 8u
#define POT_STABLE_SCANS 3u
#define POT_HYSTERESIS 3u
#define POT_FAST_CHANGE_THRESHOLD 8u
#define POT_EDGE_DEADBAND 4u
#define MIDI_CC_START 16u
#define MIDI_CC_MAX_VALUE 127u
/* SK6812 mini-e RGB LED chain driven via TIM3 CH1 PWM + DMA.
 * MCU runs at 32 MHz (HSI/2). TIM3 period = 39 → 32MHz/40 = 800 kHz (1.25 µs/bit).
 * T0H = 10/40 = 0.3125 µs, T1H = 20/40 = 0.625 µs, reset = 200 slots = 250 µs. */
#define SK6812_LED_COUNT        MATRIX_KEYS
#define SK6812_BITS_PER_LED     24u                 /* GRB, 8 bits each, no white channel */
#define SK6812_RESET_SLOTS      200u                /* 200 × 1.25 µs = 250 µs > 80 µs min */
#define SK6812_PREAMBLE_SLOTS   1u                  /* absorbs the phantom CC1 trigger at timer start */
#define SK6812_DMA_BUF_SIZE     (SK6812_PREAMBLE_SLOTS + (SK6812_LED_COUNT * SK6812_BITS_PER_LED) + SK6812_RESET_SLOTS)
#define SK6812_DATA_Pin         GPIO_PIN_6
#define SK6812_DATA_GPIO_Port   GPIOC
#define SK6812_DATA_GPIO_AF     GPIO_AF2_TIM3
#define SK6812_TIMER_PERIOD     39u
#define SK6812_T0H              10u                 /* compare value for a 0-bit */
#define SK6812_T1H              20u                 /* compare value for a 1-bit */
#define SK6812_IDLE_BRIGHTNESS  28u                 /* lower current draw while all 16 LEDs are on */
#define SK6812_IDLE_STEP        4u
#define SK6812_BRIGHTNESS       40u                 /* 0–255, applies to all key colours */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

COM_InitTypeDef BspCOMInit;
ADC_HandleTypeDef hadc1;
DMA_NodeTypeDef Node_GPDMA1_Channel0;
DMA_QListTypeDef List_GPDMA1_Channel0;
DMA_HandleTypeDef handle_GPDMA1_Channel0;

SPI_HandleTypeDef hspi1;

TIM_HandleTypeDef htim6;

PCD_HandleTypeDef hpcd_USB_DRD_FS;

/* USER CODE BEGIN PV */
static TIM_HandleTypeDef htim3;
DMA_HandleTypeDef handle_GPDMA1_Channel1;
static volatile uint8_t adc_buffer[NUM_POTS] = {0u};
static uint8_t last_midi_values[NUM_POTS] = {0u};
static uint8_t pot_candidate_values[NUM_POTS] = {0u};
static uint8_t pot_candidate_counts[NUM_POTS] = {0u};
static uint8_t sk6812_led_rgb[SK6812_LED_COUNT][3] = {0u};
static uint16_t sk6812_dma_buffer[SK6812_DMA_BUF_SIZE] = {0u};
static volatile uint8_t sk6812_dma_busy = 0u;
static uint16_t sk6812_last_key_mask = 0xFFFFu;
static uint32_t sk6812_last_button_activity = 0u;
static uint32_t sk6812_last_rainbow_frame = 0u;
static uint8_t sk6812_rainbow_offset = 0u;
static uint8_t sk6812_idle_rainbow_active = 0u;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_GPDMA1_Init(void);
static void MX_SPI1_Init(void);
static void MX_ADC1_Init(void);
static void MX_TIM6_Init(void);
static void MX_USB_PCD_Init(void);
/* USER CODE BEGIN PFP */
static void ADC_Start(void);
static uint8_t Pot_NormalizeMidiValue(uint8_t adc_value);
static void ProcessPotentiometers(void);
static void Matrix_SendNoteMessage(uint8_t note, uint8_t velocity, uint8_t pressed);
static uint16_t Matrix_ScanRaw(void);
static void Matrix_UpdateDebounce(uint16_t raw_keys, uint16_t *stable_keys, uint8_t debounce_count[MATRIX_KEYS]);
static void MX_TIM3_Init(void);
static void SK6812_SetLedColor(uint8_t led_index, uint8_t r, uint8_t g, uint8_t b);
static HAL_StatusTypeDef SK6812_Show(void);
static void SK6812_FillDmaBuffer(void);
static void SK6812_DataPinAsTimer(void);
static void SK6812_DataPinAsGpioLow(void);
static void SK6812_Clear(void);
static void SK6812_ColorWheel(uint8_t position, uint8_t *r, uint8_t *g, uint8_t *b, uint8_t brightness);
static void SK6812_SetPressedKeyColors(uint16_t key_mask);
static void SK6812_SetIdleRainbowFrame(uint8_t offset);
static void SK6812_UpdateLeds(uint16_t key_mask, uint32_t now);
void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static void MX_TIM3_Init(void)
{
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 0u;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = SK6812_TIMER_PERIOD;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

  if (HAL_TIM_PWM_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }

  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }

  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0u;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }

  HAL_TIM_MspPostInit(&htim3);
  SK6812_DataPinAsGpioLow();
}

static void SK6812_DataPinAsTimer(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOC_CLK_ENABLE();

  GPIO_InitStruct.Pin = SK6812_DATA_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = SK6812_DATA_GPIO_AF;
  HAL_GPIO_Init(SK6812_DATA_GPIO_Port, &GPIO_InitStruct);
}

static void SK6812_DataPinAsGpioLow(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOC_CLK_ENABLE();
  HAL_GPIO_WritePin(SK6812_DATA_GPIO_Port, SK6812_DATA_Pin, GPIO_PIN_RESET);

  GPIO_InitStruct.Pin = SK6812_DATA_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(SK6812_DATA_GPIO_Port, &GPIO_InitStruct);
  HAL_GPIO_WritePin(SK6812_DATA_GPIO_Port, SK6812_DATA_Pin, GPIO_PIN_RESET);
}

static void SK6812_SetLedColor(uint8_t led_index, uint8_t r, uint8_t g, uint8_t b)
{
  if (led_index >= SK6812_LED_COUNT)
  {
    return;
  }

  sk6812_led_rgb[led_index][0] = r;
  sk6812_led_rgb[led_index][1] = g;
  sk6812_led_rgb[led_index][2] = b;
}

static void SK6812_FillDmaBuffer(void)
{
  uint32_t dma_index = SK6812_PREAMBLE_SLOTS;  /* slot 0 stays 0 (preamble) */

  for (uint32_t led = 0u; led < SK6812_LED_COUNT; led++)
  {
    uint8_t grb[3] =
    {
      sk6812_led_rgb[led][1],
      sk6812_led_rgb[led][0],
      sk6812_led_rgb[led][2]
    };

    for (uint32_t color = 0u; color < 3u; color++)
    {
      for (int32_t bit = 7; bit >= 0; bit--)
      {
        sk6812_dma_buffer[dma_index++] =
          ((grb[color] & (uint8_t)(1u << bit)) != 0u) ? SK6812_T1H : SK6812_T0H;
      }
    }
  }

  while (dma_index < SK6812_DMA_BUF_SIZE)
  {
    sk6812_dma_buffer[dma_index++] = 0u;
  }
}

static HAL_StatusTypeDef SK6812_Show(void)
{
  if (sk6812_dma_busy != 0u)
  {
    return HAL_BUSY;
  }

  SK6812_FillDmaBuffer();
  SK6812_DataPinAsTimer();
  __HAL_TIM_SET_COUNTER(&htim3, 0u);
  sk6812_dma_busy = 1u;

  if (HAL_TIM_PWM_Start_DMA(&htim3,
                            TIM_CHANNEL_1,
                            (const uint32_t *)(const void *)sk6812_dma_buffer,
                            SK6812_DMA_BUF_SIZE * sizeof(uint16_t)) != HAL_OK)
  {
    sk6812_dma_busy = 0u;
    SK6812_DataPinAsGpioLow();
    return HAL_ERROR;
  }

  return HAL_OK;
}

static void SK6812_Clear(void)
{
  for (uint8_t led = 0u; led < SK6812_LED_COUNT; led++)
  {
    SK6812_SetLedColor(led, 0u, 0u, 0u);
  }
}

static void SK6812_ColorWheel(uint8_t position, uint8_t *r, uint8_t *g, uint8_t *b, uint8_t brightness)
{
  uint8_t wheel = (uint8_t)(255u - position);
  uint8_t red;
  uint8_t green;
  uint8_t blue;

  if (wheel < 85u)
  {
    red = (uint8_t)(255u - (wheel * 3u));
    green = 0u;
    blue = (uint8_t)(wheel * 3u);
  }
  else if (wheel < 170u)
  {
    wheel = (uint8_t)(wheel - 85u);
    red = 0u;
    green = (uint8_t)(wheel * 3u);
    blue = (uint8_t)(255u - (wheel * 3u));
  }
  else
  {
    wheel = (uint8_t)(wheel - 170u);
    red = (uint8_t)(wheel * 3u);
    green = (uint8_t)(255u - (wheel * 3u));
    blue = 0u;
  }

  *r = (uint8_t)(((uint16_t)red * brightness) / 255u);
  *g = (uint8_t)(((uint16_t)green * brightness) / 255u);
  *b = (uint8_t)(((uint16_t)blue * brightness) / 255u);
}

static void SK6812_UpdateLeds(uint16_t key_mask, uint32_t now)
{
  if (sk6812_dma_busy != 0u)
  {
    return;
  }

  if (sk6812_last_button_activity == 0u)
  {
    sk6812_last_button_activity = now;
  }

  if (key_mask != 0u)
  {
    sk6812_last_button_activity = now;
    sk6812_idle_rainbow_active = 0u;

    if (key_mask == sk6812_last_key_mask)
    {
      return;
    }

    SK6812_SetPressedKeyColors(key_mask);
    sk6812_last_key_mask = key_mask;
    (void)SK6812_Show();
    return;
  }

  if (sk6812_last_key_mask != 0u)
  {
    sk6812_last_button_activity = now;
    sk6812_idle_rainbow_active = 0u;
    sk6812_last_key_mask = 0u;
    SK6812_Clear();
    (void)SK6812_Show();
    return;
  }

  if ((now - sk6812_last_button_activity) < MATRIX_IDLE_RAINBOW_DELAY_MS)
  {
    return;
  }

  if ((sk6812_idle_rainbow_active != 0u) &&
      ((now - sk6812_last_rainbow_frame) < MATRIX_IDLE_RAINBOW_FRAME_MS))
  {
    return;
  }

  SK6812_SetIdleRainbowFrame(sk6812_rainbow_offset);
  sk6812_rainbow_offset = (uint8_t)(sk6812_rainbow_offset + SK6812_IDLE_STEP);
  sk6812_last_rainbow_frame = now;
  sk6812_idle_rainbow_active = 1u;
  (void)SK6812_Show();
}

static void SK6812_SetPressedKeyColors(uint16_t key_mask)
{
  SK6812_Clear();

  for (uint8_t led = 0u; led < SK6812_LED_COUNT; led++)
  {
    if ((key_mask & (uint16_t)(1u << led)) != 0u)
    {
      uint8_t r;
      uint8_t g;
      uint8_t b;
      SK6812_ColorWheel((uint8_t)(led * (256u / SK6812_LED_COUNT)), &r, &g, &b, SK6812_BRIGHTNESS);
      SK6812_SetLedColor(led, r, g, b);
    }
  }
}

static void SK6812_SetIdleRainbowFrame(uint8_t offset)
{
  for (uint8_t led = 0u; led < SK6812_LED_COUNT; led++)
  {
    uint8_t row = (uint8_t)(led / MATRIX_COLS);
    uint8_t col = (uint8_t)(led % MATRIX_COLS);
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t position = (uint8_t)(offset + (col * 28u) + (row * 18u));

    SK6812_ColorWheel(position, &r, &g, &b, SK6812_IDLE_BRIGHTNESS);
    SK6812_SetLedColor(led, r, g, b);
  }
}

static void ADC_Start(void)
{
  if (HAL_TIM_Base_Start(&htim6) != HAL_OK)
  {
    Error_Handler();
  }

  if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_buffer, NUM_POTS) != HAL_OK)
  {
    Error_Handler();
  }
}

static uint8_t Pot_NormalizeMidiValue(uint8_t adc_value)
{
  uint8_t midi_value = adc_value >> 1;

  if (midi_value <= POT_EDGE_DEADBAND)
  {
    return 0u;
  }

  if (midi_value >= (MIDI_CC_MAX_VALUE - POT_EDGE_DEADBAND))
  {
    return MIDI_CC_MAX_VALUE;
  }

  return midi_value;
}

static void ProcessPotentiometers(void)
{
  static uint32_t last_pot_process_time = 0u;
  uint32_t now = HAL_GetTick();

  if ((now - last_pot_process_time) < POT_PROCESS_MS)
  {
    return;
  }

  last_pot_process_time = now;

  for (uint8_t i = 0u; i < NUM_POTS; i++)
  {
    uint8_t new_value = Pot_NormalizeMidiValue(adc_buffer[i]);
    int16_t diff = (int16_t)new_value - (int16_t)last_midi_values[i];

    if (diff < 0)
    {
      diff = -diff;
    }

    if ((uint16_t)diff < POT_HYSTERESIS)
    {
      pot_candidate_values[i] = new_value;
      pot_candidate_counts[i] = 0u;
      continue;
    }

    if ((uint16_t)diff < POT_FAST_CHANGE_THRESHOLD)
    {
      if (new_value != pot_candidate_values[i])
      {
        pot_candidate_values[i] = new_value;
        pot_candidate_counts[i] = 1u;
        continue;
      }

      if (pot_candidate_counts[i] < POT_STABLE_SCANS)
      {
        pot_candidate_counts[i]++;
      }

      if (pot_candidate_counts[i] < POT_STABLE_SCANS)
      {
        continue;
      }
    }
    else
    {
      pot_candidate_values[i] = new_value;
      pot_candidate_counts[i] = POT_STABLE_SCANS;
    }

    if ((uint16_t)diff >= POT_HYSTERESIS)
    {
      if (tud_mounted())
      {
        uint8_t msg[3] = {0xB0u, (uint8_t)(MIDI_CC_START + i), new_value};
        tud_midi_stream_write(0, msg, 3);
      }

      last_midi_values[i] = new_value;
    }
  }
}

static void Matrix_SendNoteMessage(uint8_t note, uint8_t velocity, uint8_t pressed)
{
  if (!tud_mounted())
  {
    return;
  }

  if (pressed != 0u)
  {
    uint8_t msg[3] = {0x90u, note, velocity};
    tud_midi_stream_write(0, msg, 3);
  }
  else
  {
    uint8_t msg[3] = {0x80u, note, 0u};
    tud_midi_stream_write(0, msg, 3);
  }
}

static uint16_t Matrix_ScanRaw(void)
{
  uint16_t raw_keys = 0u;

  MCP_Write(MCP_OLATA, 0x00u);

  for (uint8_t col = 0u; col < MATRIX_COLS; col++)
  {
    uint8_t active_column = (uint8_t)(1u << col);
    uint8_t column_iodir = (uint8_t)(MATRIX_PORTA_UNUSED_INPUT_MASK |
                                     (MATRIX_COLUMN_MASK & (uint8_t)~active_column));
    uint8_t rows;

    MCP_Write(MCP_IODIRA, column_iodir);

    for (volatile uint32_t settle = 0u; settle < 500u; settle++)
    {
      __NOP();
    }

    rows = (uint8_t)(~MCP_Read(MCP_GPIOB)) & MATRIX_ROW_MASK;

    for (uint8_t row = 0u; row < MATRIX_ROWS; row++)
    {
      if ((rows & (1u << row)) != 0u)
      {
        uint8_t key_index = (uint8_t)((row * MATRIX_COLS) + col);
        raw_keys |= (uint16_t)(1u << key_index);
      }
    }
  }

  MCP_Write(MCP_IODIRA, MATRIX_ALL_COLUMNS_INPUT_IODIR);

  return raw_keys;
}

static void Matrix_UpdateDebounce(uint16_t raw_keys, uint16_t *stable_keys, uint8_t debounce_count[MATRIX_KEYS])
{
  for (uint8_t key = 0u; key < MATRIX_KEYS; key++)
  {
    uint16_t mask = (uint16_t)(1u << key);
    uint8_t raw_pressed = ((raw_keys & mask) != 0u) ? 1u : 0u;
    uint8_t stable_pressed = (((*stable_keys) & mask) != 0u) ? 1u : 0u;

    if (raw_pressed == stable_pressed)
    {
      debounce_count[key] = 0u;
      continue;
    }

    if (debounce_count[key] < MATRIX_DEBOUNCE_SCANS)
    {
      debounce_count[key]++;
    }

    if (debounce_count[key] >= MATRIX_DEBOUNCE_SCANS)
    {
      uint8_t note = (uint8_t)(MIDI_BASE_NOTE + key);

      debounce_count[key] = 0u;

      if (raw_pressed != 0u)
      {
        *stable_keys |= mask;
      }
      else
      {
        *stable_keys &= (uint16_t)~mask;
      }

      Matrix_SendNoteMessage(note, MIDI_NOTE_VELOCITY, raw_pressed);
    }
  }
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_GPDMA1_Init();
  MX_SPI1_Init();
  MX_ADC1_Init();
  MX_TIM6_Init();
  MX_USB_PCD_Init();
  /* USER CODE BEGIN 2 */

  // 1. Configure GPIOs (PA11=DM, PA12=DP)
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  __HAL_RCC_GPIOA_CLK_ENABLE();

  GPIO_InitStruct.Pin = GPIO_PIN_11 | GPIO_PIN_12;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF10_USB;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  // 2. Enable VDDUSB Power
  HAL_PWREx_EnableVddUSB();

  // 2b. Configure USB Clock Source to HSI48
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};
  PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_USB;
  PeriphClkInitStruct.UsbClockSelection = RCC_USBCLKSOURCE_HSI48;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  // 3. Enable USB Clock
  __HAL_RCC_USB_CLK_ENABLE();

  // 4. Set Interrupt Priority
  HAL_NVIC_SetPriority(USB_DRD_FS_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(USB_DRD_FS_IRQn);

  // 5. Configure Clock Recovery System (CRS) for HSI48
  __HAL_RCC_CRS_CLK_ENABLE();
  RCC_CRSInitTypeDef RCC_CRSInitStruct = {0};
  RCC_CRSInitStruct.Prescaler = RCC_CRS_SYNC_DIV1;
  RCC_CRSInitStruct.Source = RCC_CRS_SYNC_SOURCE_USB;
  RCC_CRSInitStruct.Polarity = RCC_CRS_SYNC_POLARITY_RISING;
  RCC_CRSInitStruct.ReloadValue = __HAL_RCC_CRS_RELOADVALUE_CALCULATE(48000000, 1000);
  RCC_CRSInitStruct.ErrorLimitValue = 34;
  RCC_CRSInitStruct.HSI48CalibrationValue = 32;
  HAL_RCCEx_CRSConfig(&RCC_CRSInitStruct);

  // 6. Explicitly Enable HSI48 just in case
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI48;
  RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;
  HAL_RCC_OscConfig(&RCC_OscInitStruct);

  /* Initialize TinyUSB (handles USB peripheral initialization) */
  tusb_init();

  // Wait for 100ms for stable power-up
  HAL_Delay(100);

  // Initialize SPI1 explicitly afterwards
  MX_SPI1_Init();

  // Initialize MCP23S17 (SPI IO Expander)
  MCP_Init();

  MX_TIM3_Init();
  ADC_Start();

  SK6812_Clear();
  if (SK6812_Show() != HAL_OK)
  {
    Error_Handler();
  }

  /* USER CODE END 2 */

  /* Initialize USER push-button, will be used to trigger an interrupt each time it's pressed.*/
  BSP_PB_Init(BUTTON_USER, BUTTON_MODE_EXTI);

  /* Initialize COM1 port (115200, 8 bits (7-bit data + 1 stop bit), no parity */
  BspCOMInit.BaudRate   = 115200;
  BspCOMInit.WordLength = COM_WORDLENGTH_8B;
  BspCOMInit.StopBits   = COM_STOPBITS_1;
  BspCOMInit.Parity     = COM_PARITY_NONE;
  BspCOMInit.HwFlowCtl  = COM_HWCONTROL_NONE;
  if (BSP_COM_Init(COM1, &BspCOMInit) != BSP_ERROR_NONE)
  {
    Error_Handler();
  }

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */

  uint16_t raw_keys = 0;
  uint16_t stable_keys = 0;
  uint8_t debounce_count[MATRIX_KEYS] = {0};
  uint8_t mcp_ready = MCP_IsReady();
  uint32_t last_scan_time = 0;
  uint32_t last_mcp_retry_time = 0;

  while (1)
  {
    /* TinyUSB device task */
    tud_task();
    uint32_t now = HAL_GetTick();

    // Retry MCP detection if wiring is fixed after boot.
    if (!mcp_ready)
    {
      if ((now - last_mcp_retry_time) >= 1000u)
      {
        last_mcp_retry_time = now;
        MCP_Init();
        mcp_ready = MCP_IsReady();
      }
    }

    ProcessPotentiometers();

    // Matrix scan + debounce + edge detection
    if (mcp_ready)
    {
      if ((now - last_scan_time) >= MATRIX_SCAN_MS)
      {
        last_scan_time = now;
        raw_keys = Matrix_ScanRaw();
        Matrix_UpdateDebounce(raw_keys, &stable_keys, debounce_count);
      }
    }

    SK6812_UpdateLeds(stable_keys, now);

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI48|RCC_OSCILLATORTYPE_HSI
                              |RCC_OSCILLATORTYPE_CSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSIDiv = RCC_HSI_DIV2;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;
  RCC_OscInitStruct.CSIState = RCC_CSI_ON;
  RCC_OscInitStruct.CSICalibrationValue = RCC_CSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLL1_SOURCE_CSI;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 129;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1_VCIRANGE_2;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1_VCORANGE_WIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_PCLK3;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure the programming delay
  */
  __HAL_FLASH_SET_PROGRAM_DELAY(FLASH_PROGRAMMING_DELAY_0);
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Common config
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV1;
  hadc1.Init.Resolution = ADC_RESOLUTION_8B;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.ScanConvMode = ADC_SCAN_ENABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  hadc1.Init.LowPowerAutoWait = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.NbrOfConversion = 8;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_EXTERNALTRIG_T6_TRGO;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_RISING;
  hadc1.Init.DMAContinuousRequests = ENABLE;
  hadc1.Init.SamplingMode = ADC_SAMPLING_MODE_NORMAL;
  hadc1.Init.Overrun = ADC_OVR_DATA_PRESERVED;
  hadc1.Init.OversamplingMode = DISABLE;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_0;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_24CYCLES_5;
  sConfig.SingleDiff = ADC_SINGLE_ENDED;
  sConfig.OffsetNumber = ADC_OFFSET_NONE;
  sConfig.Offset = 0;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_1;
  sConfig.Rank = ADC_REGULAR_RANK_2;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_5;
  sConfig.Rank = ADC_REGULAR_RANK_3;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_8;
  sConfig.Rank = ADC_REGULAR_RANK_4;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_9;
  sConfig.Rank = ADC_REGULAR_RANK_5;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_10;
  sConfig.Rank = ADC_REGULAR_RANK_6;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_11;
  sConfig.Rank = ADC_REGULAR_RANK_7;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_12;
  sConfig.Rank = ADC_REGULAR_RANK_8;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief GPDMA1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPDMA1_Init(void)
{

  /* USER CODE BEGIN GPDMA1_Init 0 */

  /* USER CODE END GPDMA1_Init 0 */

  /* Peripheral clock enable */
  __HAL_RCC_GPDMA1_CLK_ENABLE();

  /* GPDMA1 interrupt Init */
    HAL_NVIC_SetPriority(GPDMA1_Channel0_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(GPDMA1_Channel0_IRQn);

  /* USER CODE BEGIN GPDMA1_Init 1 */
  HAL_NVIC_SetPriority(GPDMA1_Channel1_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(GPDMA1_Channel1_IRQn);

  /* USER CODE END GPDMA1_Init 1 */
  /* USER CODE BEGIN GPDMA1_Init 2 */

  /* USER CODE END GPDMA1_Init 2 */

}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_64;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 0x7;
  hspi1.Init.NSSPMode = SPI_NSS_PULSE_ENABLE;
  hspi1.Init.NSSPolarity = SPI_NSS_POLARITY_LOW;
  hspi1.Init.FifoThreshold = SPI_FIFO_THRESHOLD_01DATA;
  hspi1.Init.MasterSSIdleness = SPI_MASTER_SS_IDLENESS_00CYCLE;
  hspi1.Init.MasterInterDataIdleness = SPI_MASTER_INTERDATA_IDLENESS_00CYCLE;
  hspi1.Init.MasterReceiverAutoSusp = SPI_MASTER_RX_AUTOSUSP_DISABLE;
  hspi1.Init.MasterKeepIOState = SPI_MASTER_KEEP_IO_STATE_DISABLE;
  hspi1.Init.IOSwap = SPI_IO_SWAP_DISABLE;
  hspi1.Init.ReadyMasterManagement = SPI_RDY_MASTER_MANAGEMENT_INTERNALLY;
  hspi1.Init.ReadyPolarity = SPI_RDY_POLARITY_HIGH;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief TIM6 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM6_Init(void)
{

  /* USER CODE BEGIN TIM6_Init 0 */

  /* USER CODE END TIM6_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM6_Init 1 */

  /* USER CODE END TIM6_Init 1 */
  htim6.Instance = TIM6;
  htim6.Init.Prescaler = 249;
  htim6.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim6.Init.Period = 999;
  htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim6) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_UPDATE;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim6, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM6_Init 2 */

  /* USER CODE END TIM6_Init 2 */

}

/**
  * @brief USB Initialization Function
  * @param None
  * @retval None
  */
static void MX_USB_PCD_Init(void)
{

  /* USER CODE BEGIN USB_Init 0 */

  /* USER CODE END USB_Init 0 */

  /* USER CODE BEGIN USB_Init 1 */

  /* USER CODE END USB_Init 1 */
  hpcd_USB_DRD_FS.Instance = USB_DRD_FS;
  hpcd_USB_DRD_FS.Init.dev_endpoints = 8;
  hpcd_USB_DRD_FS.Init.speed = USBD_FS_SPEED;
  hpcd_USB_DRD_FS.Init.phy_itface = PCD_PHY_EMBEDDED;
  hpcd_USB_DRD_FS.Init.Sof_enable = DISABLE;
  hpcd_USB_DRD_FS.Init.low_power_enable = DISABLE;
  hpcd_USB_DRD_FS.Init.lpm_enable = DISABLE;
  hpcd_USB_DRD_FS.Init.battery_charging_enable = DISABLE;
  hpcd_USB_DRD_FS.Init.vbus_sensing_enable = DISABLE;
  hpcd_USB_DRD_FS.Init.bulk_doublebuffer_enable = DISABLE;
  hpcd_USB_DRD_FS.Init.iso_singlebuffer_enable = DISABLE;
  if (HAL_PCD_Init(&hpcd_USB_DRD_FS) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USB_Init 2 */

  /* USER CODE END USB_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_9, GPIO_PIN_SET);

  /*Configure GPIO pin : PC9 */
  GPIO_InitStruct.Pin = GPIO_PIN_9;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : PC4 */
  GPIO_InitStruct.Pin = GPIO_PIN_4;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM3)
  {
    (void)HAL_TIM_PWM_Stop_DMA(htim, TIM_CHANNEL_1);
    __HAL_TIM_SET_COMPARE(htim, TIM_CHANNEL_1, 0u);
    htim->Instance->CR1 &= ~0x0001U;
    __HAL_TIM_SET_COUNTER(htim, 0u);
    SK6812_DataPinAsGpioLow();
    sk6812_dma_busy = 0u;
  }
}

void HAL_TIM_PWM_PulseFinishedHalfCpltCallback(TIM_HandleTypeDef *htim)
{
  (void)htim;
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
