#include "py32f0xx.h"

// dummy _close, _lseek, _read, _write functions, avoids linker errors
int _close(int file) { return -1; }

int _lseek(int file, int ptr, int dir) { return 0; }

int _read(int file, char *ptr, int len) { return 0; }

int _write(int file, char *ptr, int len) { return 0; }


void HAL_MspInit(void) {
}

void HAL_TIM_Base_MspInit(TIM_HandleTypeDef *htim) {
    __HAL_RCC_TIM1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    HAL_GPIO_Init(GPIOA, &(GPIO_InitTypeDef){
                             .Mode = GPIO_MODE_OUTPUT_PP,
                             .Pull = GPIO_PULLUP,
                             .Speed = GPIO_SPEED_FREQ_HIGH,
                             .Pin = GPIO_PIN_1,
                             .Alternate = GPIO_AF13_TIM1,
                         });
}
