#include "logic.h"
#include "py32f0xx.h"


void SysTick_Handler() {
    HAL_IncTick();
}

void checkOk(int ok) {
    if (ok != HAL_OK) {
        __asm__("bkpt #0");
        while (1) { __NOP(); }
    }
}

static const int SYSCLOCK_FREQ_HZ = (int) 12e6;

static void APP_SystemClockConfig(void) {
    /** use internal oscillator, sysclk = 16MHz */
    checkOk(HAL_RCC_OscConfig(&(RCC_OscInitTypeDef){
        .OscillatorType = RCC_OSCILLATORTYPE_HSI,
        .HSIState = RCC_HSI_ON,
        .HSIDiv = RCC_HSI_DIV2, /* 12MHz */
        .HSICalibrationValue = RCC_HSICALIBRATION_24MHz,
    }));
    // make sure we have a LSI for the watchdog
    checkOk(HAL_RCC_OscConfig(&(RCC_OscInitTypeDef){
        .OscillatorType = RCC_OSCILLATORTYPE_LSI,
        .LSIState = RCC_LSI_ON,
    }));

    checkOk(HAL_RCC_ClockConfig(
        &(RCC_ClkInitTypeDef){
            .ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1,
            .SYSCLKSource = RCC_SYSCLKSOURCE_HSI, /* SYSCLK source */
            .AHBCLKDivider = RCC_SYSCLK_DIV1,
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
        .ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4,            /* Analog ADC clock source is PCLK*/
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

    // PA3 is TEMP_SENSE
    HAL_GPIO_Init(
        GPIOA,
        &(GPIO_InitTypeDef){
            .Mode = GPIO_MODE_ANALOG,
            .Pin = GPIO_PIN_3});
    // PA4 is FAN_SENSE
    HAL_GPIO_Init(
        GPIOA,
        &(GPIO_InitTypeDef){
            .Mode = GPIO_MODE_ANALOG,
            .Pin = GPIO_PIN_4});

    checkOk(HAL_ADC_Calibration_Start(&hadc1));
    checkOk(HAL_ADC_Init(&hadc1));
    checkOk(HAL_ADC_ConfigChannel(&hadc1, &(ADC_ChannelConfTypeDef){
                                              .Rank = ADC_RANK_CHANNEL_NUMBER,
                                              .Channel = ADC_CHANNEL_3,
                                          }));
}


static const int PWM_PERIOD = (SYSCLOCK_FREQ_HZ / PWM_FREQ_HZ) - 1;

TIM_HandleTypeDef htim1 = {
    .Instance = TIM1,
    .Init = {
        .Period = PWM_PERIOD,
        .Prescaler = 0,
        .ClockDivision = TIM_CLOCKDIVISION_DIV1,
        .CounterMode = TIM_COUNTERMODE_UP,
        .RepetitionCounter = 0,
        .AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE,
    },
};

static void APP_PwmOutConfig() {
    __HAL_RCC_TIM1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    // PWM output 200kHz
    // Pin 7, PA1, TIM1_CH4
    HAL_GPIO_Init(
        GPIOA,
        &(GPIO_InitTypeDef){
            .Mode = GPIO_MODE_AF_PP,
            .Pull = GPIO_PULLUP,
            .Speed = GPIO_SPEED_FREQ_HIGH,
            .Pin = GPIO_PIN_1,
            .Alternate = GPIO_AF13_TIM1,
        });

    checkOk(HAL_TIM_Base_Init(&htim1));
    checkOk(HAL_TIM_PWM_ConfigChannel(
        &htim1,
        &(TIM_OC_InitTypeDef){
            .OCMode = TIM_OCMODE_PWM1,
            .OCFastMode = TIM_OCFAST_DISABLE,
            .OCPolarity = TIM_OCPOLARITY_HIGH,
            .OCNPolarity = TIM_OCNPOLARITY_LOW,
            .OCIdleState = TIM_OCIDLESTATE_RESET,
            .OCNIdleState = TIM_OCNIDLESTATE_RESET,
            .Pulse = 0,// duty cycle = 0%
        },
        TIM_CHANNEL_4));
    checkOk(HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_4));
}

static void setPwmDutyCycle(double dutyCycle) {
    if (dutyCycle < 0.0) {
        dutyCycle = 0.0;
    } else if (dutyCycle > 1.0) {
        dutyCycle = 1.0;
    }
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_4, (uint32_t) (dutyCycle * PWM_PERIOD));
}

typedef struct {
    uint32_t tempCounts;
    uint32_t fanCounts;
} AdcResults;

AdcResults readAdc() {
    checkOk(HAL_ADC_Start(&hadc1));

    uint32_t allTempCounts = 0;
    uint32_t allFanCounts = 0;
    static const int NUM_SAMPLES = 64;
    for (int i = 0; i < NUM_SAMPLES; i++) {
        // 80us/conversion, 10ms total
        checkOk(HAL_ADC_PollForConversion(&hadc1, 1));
        allTempCounts += HAL_ADC_GetValue(&hadc1);
        checkOk(HAL_ADC_PollForConversion(&hadc1, 1));
        allFanCounts += HAL_ADC_GetValue(&hadc1);
    }
    return (AdcResults){
        .tempCounts = allTempCounts / NUM_SAMPLES,
        .fanCounts = allFanCounts / NUM_SAMPLES};
}


int main(void) {
    HAL_Init();
    APP_SystemClockConfig();
    APP_Watchdog();
    APP_AdcConfig();
    APP_PwmOutConfig();
    SystemCoreClockUpdate();

    const Config config = {

        // 25% min works well for 12V fan
        // 4% min works well for 24V fan
        .fanMinDutyCycle = .04,
        .fanMaxDutyCycle = 1.,
        .fanSpinupDutyCycle = 1.,
        .fanSpinupTimeMs = 1500,

        .tempMinC = 40,
        .tempMaxC = 85,
        .tempHysteresisC = 8,
    };
    const PtcThermistorConfig thermistorConfig = PTC_THERMISTOR_10K_3950;

    State state = {
        .state = FAN_OFF,
        .lastChangeTimeMs = 0,
        .lastFilteredTempC = 25.,
    };

    while (1) {
        uint32_t startTime = HAL_GetTick();
        AdcResults adcResults = readAdc();

        double tempC = tempCountsToC(adcResults.tempCounts, &thermistorConfig);
        double outputRatio = fanVoltageRatio(tempC, HAL_GetTick(), &config, &state);
        double dutyCycle = ratioToDcmBuckDutyCycle(outputRatio);
        setPwmDutyCycle(dutyCycle);

        // 10ms per loop (will mess up at 49-day uptime rollover, but that's ok)
        uint32_t elapsed = HAL_GetTick() - startTime;
        if (elapsed < 10) {
            HAL_Delay(10 - elapsed);
        }
        HAL_IWDG_Refresh(&hiwdg);
    }
}
