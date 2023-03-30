#include "py32f0xx.h"

// dummy _close, _lseek, _read, _write functions, avoids linker errors
int _close(int file) { return -1; }

int _lseek(int file, int ptr, int dir) { return 0; }

int _read(int file, char *ptr, int len) { return 0; }

int _write(int file, char *ptr, int len) { return 0; }


void HAL_MspInit(void) {
}

void HAL_TIM_Base_MspInit(TIM_HandleTypeDef *htim) {
}
