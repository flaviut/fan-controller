#include "py32f0xx.h"
#include <sys/cdefs.h>

#pragma ide diagnostic ignored "bugprone-reserved-identifier"

// dummy _close, _lseek, _read, _write functions, avoids linker errors
int _close(int file) { return -1; }

int _lseek(int file, int ptr, int dir) { return 0; }

int _read(int file, char *ptr, int len) { return 0; }

int _write(int file, char *ptr, int len) { return 0; }

// dummy _fstat, _getpid, _isatty, _kill, avoid linker warnings
int _fstat(int file, void *st) { return 0; }

int _getpid(void) { return 1; }

int _isatty(int file) { return 1; }

int _kill(int pid, int sig) { return 0; }

int __errno;

void HAL_MspInit(void) {
}

void HAL_TIM_Base_MspInit(TIM_HandleTypeDef *htim) {
}

_Noreturn void __assert_func(const char *file, int line, const char *func, const char *failedexpr) {
    // if we have a debugger attached, break into it, otherwise the watchdog will reset us
    while (1) { __asm__("bkpt"); }
}
