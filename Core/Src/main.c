/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
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
#include "crc.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
#pragma pack(push, 1)
typedef struct {
    uint8_t  sync;      // Sync byte: 0x5A
    uint8_t  len;       // Length of the frame
    uint8_t  type;      // Type: 0x01
    uint16_t ch[4];     // 4-channel data (Little Endian)
    uint8_t  sw;        // Four front 3-position switches
    uint8_t  sh;        // Two shoulder 2-position switches
    uint16_t buttons;   // Three 5-way keys, little-endian bitfield
    uint8_t  crc;       // CRC8 Checksum
} CRSF_Frame_t;
#pragma pack(pop)

typedef enum {
    STATE_WAIT_START,
    STATE_ACTIVE
} SystemState_t;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
enum {
    ANALOG_NEUTRAL_VALUE = 2048U,
    INPUT_FRAME_SYNC = 0x5A,
    INPUT_FRAME_TYPE = 0x01,
    INPUT_FRAME_LEN_EXTENDED = 0x0E
};
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
volatile SystemState_t current_state = STATE_WAIT_START;
volatile uint8_t sample_due = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/**
 * @brief  Hardware CRC calculation (for 8-bit DVB-S2)
 * @param  data: Pointer to data buffer
 * @param  len: Length of data
 * @retval 8-bit CRC result
 */
uint8_t calculate_crc8(const uint8_t *data, uint32_t len) {
    // 1. Reset CRC peripheral
    __HAL_CRC_DR_RESET(&hcrc);
    
    // 2. Input data byte by byte to avoid alignment issues
    for (uint32_t i = 0; i < len; i++) {
        *(__IO uint8_t *)(__IO void *)(&hcrc.Instance->DR) = data[i];
    }
    
    // 3. Read result
    return (uint8_t)(hcrc.Instance->DR);
}

static uint8_t pack_3pos_state(uint8_t value, uint8_t bit_offset)
{
    return (uint8_t)((value & 0x03U) << bit_offset);
}

static uint8_t is_button_pressed(GPIO_TypeDef *port, uint16_t pin)
{
    return HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_RESET;
}

static uint8_t read_three_pos_switch(GPIO_TypeDef *low_port, uint16_t low_pin,
                                     GPIO_TypeDef *high_port, uint16_t high_pin,
                                     uint8_t previous_value)
{
    uint8_t low_active = is_button_pressed(low_port, low_pin);
    uint8_t high_active = is_button_pressed(high_port, high_pin);

    if (low_active && high_active) {
        return previous_value;
    }
    if (low_active) {
        return 0U;
    }
    if (high_active) {
        return 2U;
    }
    return 1U;
}

static uint16_t read_buttons_mask(void)
{
    uint16_t buttons = 0;

    if (is_button_pressed(BTN1_UP_GPIO_Port, BTN1_UP_Pin)) {
        buttons |= (1U << 10);
    }
    if (is_button_pressed(BTN1_DOWN_GPIO_Port, BTN1_DOWN_Pin)) {
        buttons |= (1U << 11);
    }
    if (is_button_pressed(BTN1_LEFT_GPIO_Port, BTN1_LEFT_Pin)) {
        buttons |= (1U << 12);
    }
    if (is_button_pressed(BTN1_RIGHT_GPIO_Port, BTN1_RIGHT_Pin)) {
        buttons |= (1U << 13);
    }
    if (is_button_pressed(BTN1_MID_GPIO_Port, BTN1_MID_Pin)) {
        buttons |= (1U << 14);
    }

    return buttons;
}

static void fill_button_packet(CRSF_Frame_t *packet)
{
    static uint8_t switch_b_state = 1U;
    static uint8_t switch_c_state = 1U;

    packet->sync = INPUT_FRAME_SYNC;
    packet->len  = INPUT_FRAME_LEN_EXTENDED;
    packet->type = INPUT_FRAME_TYPE;

    for (uint32_t i = 0; i < 4; i++) {
        packet->ch[i] = ANALOG_NEUTRAL_VALUE;
    }

    switch_b_state = read_three_pos_switch(SB_LO_GPIO_Port, SB_LO_Pin,
                                           SB_HI_GPIO_Port, SB_HI_Pin,
                                           switch_b_state);
    switch_c_state = read_three_pos_switch(SC_HO_GPIO_Port, SC_HO_Pin,
                                           SC_HI_GPIO_Port, SC_HI_Pin,
                                           switch_c_state);

    packet->sw = (uint8_t)(pack_3pos_state(switch_b_state, 0U) |
                           pack_3pos_state(switch_c_state, 2U));
    packet->sh = 0;
    packet->buttons = read_buttons_mask();
    packet->crc = calculate_crc8(&packet->type, packet->len - 1U);
}

static uint8_t uart_rx_byte;
static const char *cmd_start = "start_stm";
static uint8_t idx_start = 0;
static const char *cmd_sleep = "sleep_stm";
static uint8_t idx_sleep = 0;

static void process_uart_byte(uint8_t rx_byte)
{
    if (current_state == STATE_WAIT_START) {
        if (rx_byte == cmd_start[idx_start]) {
            idx_start++;
            if (idx_start >= 9U) {
                current_state = STATE_ACTIVE;
                idx_start = 0;
            }
        } else if (rx_byte == (uint8_t)cmd_start[0]) {
            idx_start = 1;
        } else {
            idx_start = 0;
        }
    } else {
        if (rx_byte == cmd_sleep[idx_sleep]) {
            idx_sleep++;
            if (idx_sleep >= 9U) {
                current_state = STATE_WAIT_START;
                idx_sleep = 0;
            }
        } else if (rx_byte == (uint8_t)cmd_sleep[0]) {
            idx_sleep = 1;
        } else {
            idx_sleep = 0;
        }
    }
}

static void poll_uart_commands(void)
{
    while (HAL_UART_Receive(&huart2, &uart_rx_byte, 1, 0) == HAL_OK) {
        process_uart_byte(uart_rx_byte);
    }
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM1) {
        sample_due = 1;
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
  MX_CRC_Init();
  MX_USART2_UART_Init();
  MX_TIM1_Init();
  /* USER CODE BEGIN 2 */
  if (HAL_TIM_Base_Start_IT(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    poll_uart_commands();

    if (current_state == STATE_WAIT_START) {
        sample_due = 0;
    } else {
        if (sample_due) {
            CRSF_Frame_t packet;
            sample_due = 0;
            fill_button_packet(&packet);
            HAL_UART_Transmit(&huart2, (uint8_t*)&packet, sizeof(packet), 10);
        }
    }
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
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSIDiv = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV1;
  RCC_OscInitStruct.PLL.PLLN = 8;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

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

#ifdef  USE_FULL_ASSERT
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
