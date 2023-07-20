#ifndef FIRMWARE_LOGIC_H
#define FIRMWARE_LOGIC_H

#include "stdint.h"


enum ProcessState {
    FAN_OFF,
    FAN_SPINUP,
    FAN_ON,
};

/**
 * Based upon the "Trapezoid Control Algorithm" in https://www.mattmillman.com/projects/another-intelligent-4-wire-fan-speed-controller/
 */
typedef struct {
    /**
     * What is the minimum duty cycle that the fan should be allowed to run at?
     *
     * This should be above the fan's stall duty cycle when the fan is already
     * spinning.
     */
    float fanMinDutyCycle;
    /** What is the maximum duty cycle that the fan should be allowed to run at? */
    float fanMaxDutyCycle;
    /**
     * It is much easier to keep the fan moving than to start it from a standstill.
     *
     * This duty cycle is set when the fan is first turned on, and then the fan is
     * allowed to spin up to full speed before the duty cycle is reduced to the
     * normal operating value.
     *
     * It is recommended that this be 100%, and that the spinup time be used to tune
     * things for quiet operation.
     */
    float fanSpinupDutyCycle;
    /**
     * How long does it take the fan to spin up? This will likely be on the order of
     * 1-2 seconds.
     */
    int fanSpinupTimeMs;

    /** What is the minimum temperature that the fan should be allowed to run at? */
    int tempMinC;
    /** What is the temperature at which the fan should be running as fast as possible */
    int tempMaxC;

    /**
     * Once the fan is on, how far below the minimum temperature should we wait
     * before turning it off?
     *
     * The goal of this variable is to avoid turning the fan on and off repeatedly
     */
    int tempHysteresisC;
} Config;

typedef struct {
    enum ProcessState state;
    uint32_t lastChangeTimeMs;
    float lastFilteredTempC;
} State;

typedef struct {
    /** What is the nominal resistance of the PTC thermistor at the nominal temperature */
    int nominalOhms;
    /** What is the nominal temperature of the PTC thermistor (in Kelvin) */
    int nominalTempK;
    /** What is the beta coefficient of the PTC thermistor */
    int beta;
} PtcThermistorConfig;


static const int KELVIN_OFFSET = 273;
static const PtcThermistorConfig PTC_THERMISTOR_100K_3950 = {
    .nominalOhms = 100000,
    .nominalTempK = 25 + KELVIN_OFFSET,
    .beta = 3950,
};

int resistanceToTempC(float thermistorOhms, const PtcThermistorConfig *config);

float ratioToUnknownBridgeResistance(float voltageRatio, float knownResistance);

int tempCountsToC(uint32_t tempCounts, const PtcThermistorConfig *config);

float dutyCycle(int newTempC, uint32_t currentMs, const Config *config, State *state);


#endif//FIRMWARE_LOGIC_H
