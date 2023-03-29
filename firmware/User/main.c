#include "py32f0xx.h"
#include <assert.h>
#include <math.h>

int __errno;// NOLINT(bugprone-reserved-identifier)

void SysTick_Handler() {
    HAL_IncTick();
}

void checkOk(int ok) {
    if (ok != HAL_OK) {
        __asm__("bkpt #0");
        while (1) { __NOP(); }
    }
}

static const int SYSCLOCK_FREQ_HZ = (int) 16e6;

static void APP_SystemClockConfig(void) {
    /** use internal oscillator, sysclk = 16MHz */
    checkOk(HAL_RCC_OscConfig(&(RCC_OscInitTypeDef){
        .OscillatorType = RCC_OSCILLATORTYPE_HSI,
        .HSIState = RCC_HSI_ON,
        .HSIDiv = RCC_HSI_DIV2, /* 16MHz */
        .HSICalibrationValue = RCC_HSICALIBRATION_8MHz,
    }));
    // make sure we have a LSI for the watchdog
    checkOk(HAL_RCC_OscConfig(&(RCC_OscInitTypeDef){
        .OscillatorType = RCC_OSCILLATORTYPE_LSI,
        .LSIState = RCC_LSI_ON,
    }));

    checkOk(HAL_RCC_ClockConfig(&(RCC_ClkInitTypeDef){
                                    .ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1,
                                    .SYSCLKSource = RCC_SYSCLKSOURCE_HSI, /* SYSCLK source */
                                    .AHBCLKDivider = RCC_SYSCLK_DIV1,
                                    .APB1CLKDivider = RCC_HCLK_DIV2,
                                },
                                FLASH_LATENCY_0));// latency 0 for <= 24MHz
}

// uses LSI clock, 32kHz
IWDG_HandleTypeDef hiwdg = {
    .Instance = IWDG,
    .Init = {
        .Prescaler = IWDG_PRESCALER_256,
        .Reload = 125,// 1s
    },
};

static void APP_Watchdog() {
    checkOk(HAL_IWDG_Init(&hiwdg));
    HAL_IWDG_Refresh(&hiwdg);
}


ADC_HandleTypeDef hadc1 = {
    .Instance = ADC1,
    .Init = (ADC_InitTypeDef){
        .ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV32,           /* Analog ADC clock source is PCLK*/
        .Resolution = ADC_RESOLUTION_12B,                      /* conversion resolution 12bit*/
        .DataAlign = ADC_DATAALIGN_RIGHT,                      /* data right alignment */
        .ScanConvMode = ADC_SCAN_DIRECTION_FORWARD,            /* scan sequence direction: up (from channel 0 to channel 11)*/
        .EOCSelection = ADC_EOC_SINGLE_CONV,                   /* ADC_EOC_SINGLE_CONV: single sampling, ADC_EOC_SEQ_CONV: sequence sampling*/
        .LowPowerAutoWait = ENABLE,                            /* ENABLE=After reading the ADC value, start the next conversion , DISABLE=Direct conversion */
        .ContinuousConvMode = DISABLE,                         /* single conversion mode */
        .DiscontinuousConvMode = DISABLE,                      /* Disable discontinuous mode */
        .ExternalTrigConv = ADC_SOFTWARE_START,                /* software trigger */
        .ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE, /* No trigger edge */
        .Overrun = ADC_OVR_DATA_OVERWRITTEN,                   /* ADC_OVR_DATA_OVERWRITTEN=overrun when overloaded, ADC_OVR_DATA_PRESERVED=keep old value*/
        .SamplingTimeCommon = ADC_SAMPLETIME_239CYCLES_5,      /* channel sampling time is 239.5ADC clock cycle */
    },
};

static void APP_AdcConfig(void) {
    __HAL_RCC_ADC_FORCE_RESET();
    __HAL_RCC_ADC_RELEASE_RESET(); /* Reset ADC */
    __HAL_RCC_ADC_CLK_ENABLE();    /* Enable ADC clock */

    checkOk(HAL_ADC_Calibration_Start(&hadc1));
    checkOk(HAL_ADC_Init(&hadc1));
    checkOk(HAL_ADC_ConfigChannel(&hadc1, &(ADC_ChannelConfTypeDef){
                                              .Rank = ADC_RANK_NONE,
                                              .Channel = ADC_CHANNEL_0,
                                          }));
}

float fanCurveRatio(int tempC) {
    // every 10°C, starting at 0°C.
    static const float fanCurve[] = {
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        0.25f,
        0.35f,
        0.75f,
        1.0f,
        1.0f,
    };

    // linear interpolation between the two nearest points
    int index = tempC / 10;
    if (index >= 9) {
        return 1.0f;
    } else if (index < 0) {
        return 0.0f;
    }
    float fanSpeed =
        fanCurve[index] +
        (fanCurve[index + 1] - fanCurve[index]) * ((float) (tempC % 10)) / 10.0f;

    if (fanSpeed < 0.35f && fanSpeed > 0.0f) {
        fanSpeed = 0.35f;// never less than 35%
    }
    return fanSpeed;
}


static const int PWM_FREQ_HZ = (int) 2e5;// 200kHz

TIM_HandleTypeDef htim1 = {
    .Instance = TIM1,
    .Init = {
        .Period = (SYSCLOCK_FREQ_HZ / PWM_FREQ_HZ) - 1,
        .Prescaler = 0,
        .ClockDivision = TIM_CLOCKDIVISION_DIV1,
        .CounterMode = TIM_COUNTERMODE_UP,
        .RepetitionCounter = 0,
        .AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE,
    },
};

static void APP_PwmOutConfig() {
    // PWM output 200kHz
    // Pin 7, PA1, TIM1_CH4
    checkOk(HAL_TIM_Base_Init(&htim1));
    checkOk(HAL_TIM_PWM_ConfigChannel(&htim1, &(TIM_OC_InitTypeDef){
                                                  .OCMode = TIM_OCMODE_PWM1,
                                                  .OCFastMode = TIM_OCFAST_DISABLE,
                                                  .OCPolarity = TIM_OCPOLARITY_HIGH,
                                                  .OCNPolarity = TIM_OCNPOLARITY_HIGH,
                                                  .OCIdleState = TIM_OCIDLESTATE_RESET,
                                                  .OCNIdleState = TIM_OCNIDLESTATE_RESET,
                                                  .Pulse = 0,// duty cycle = 0%
                                              },
                                      TIM_CHANNEL_4));
    checkOk(HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_4));
}

static void setPwmDutyCycle(float dutyCycle) {
    if (dutyCycle < 0.0f) {
        dutyCycle = 0.0f;
    } else if (dutyCycle > 1.0f) {
        dutyCycle = 1.0f;
    }
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_4, (uint32_t) (dutyCycle * htim1.Init.Period));
}

int main(void) {
    HAL_Init();
    APP_SystemClockConfig();
    APP_Watchdog();
    APP_AdcConfig();
    APP_PwmOutConfig();
    SystemCoreClockUpdate();

    HAL_GPIO_Init(GPIOA, &(GPIO_InitTypeDef){
                             .Mode = GPIO_MODE_ANALOG,
                             .Pin = GPIO_PIN_3});
    HAL_GPIO_Init(GPIOA, &(GPIO_InitTypeDef){
                             .Mode = GPIO_MODE_ANALOG,
                             .Pin = GPIO_PIN_4});

    while (1) {
        uint32_t allTempCounts = 0;
        for (int i = 0; i < 100; i++) {
            checkOk(HAL_ADC_Start(&hadc1));
            checkOk(HAL_ADC_PollForConversion(&hadc1, 10000));
            allTempCounts += HAL_ADC_GetValue(&hadc1);
        }
        uint32_t avgTempCounts = allTempCounts / 100;
        static const float SUPPLY_VOLTS = 3.3f;
        float tempVolts = (float) avgTempCounts * SUPPLY_VOLTS / 4096.0f;
        static const float REFERENCE_OHMS = 100000.0f;
        float ptcResistance = REFERENCE_OHMS * (SUPPLY_VOLTS / tempVolts - 1.0f);
        // https://en.wikipedia.org/wiki/Thermistor#B_or_%CE%B2_parameter_equation
        static const float PTC_NOMINAL_OHMS = 100000.0f;
        static const float PTC_NOMINAL_TEMP = 298.15f;
        static const float PTC_BETA = 3435.0f;
        float invTempC = (1.0f / PTC_NOMINAL_TEMP) +
                         (1.0f / PTC_BETA) * logf(ptcResistance / PTC_NOMINAL_OHMS);
        float tempC = 1.0f / invTempC;


        static const float PWM_PERIOD_S = 1.0f / (float) PWM_FREQ_HZ;// 5us
        static const float INDUCTANCE_H = 1e-5f;                     // 10uH
        static const float TYP_CURRENT_A = 0.1f;                     // 100mA

        float outputPercent = fanCurveRatio((int) tempC);
        // Vo% = Vi / ((2*L*Io/(D^2*Vi*T)) + 1)
        // Vi = 1
        // D = sqrt((2*L*Io*Vo%) / ((Vo%-1) * T))
        float dutyCycle = sqrtf(
            (2.0f * INDUCTANCE_H * TYP_CURRENT_A * outputPercent) /
            ((outputPercent - 1.0f) * PWM_PERIOD_S));

        setPwmDutyCycle(dutyCycle);
        HAL_IWDG_Refresh(&hiwdg);
    }
}
