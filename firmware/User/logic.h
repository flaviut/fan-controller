#ifndef FIRMWARE_LOGIC_H
#define FIRMWARE_LOGIC_H

#include "stdint.h"

static const int PWM_FREQ_HZ = 30000;

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
    double fanMinDutyCycle;
    /** What is the maximum duty cycle that the fan should be allowed to run at? */
    double fanMaxDutyCycle;
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
    double fanSpinupDutyCycle;
    /**
     * How long does it take the fan to spin up? This will likely be on the order of
     * 1-2 seconds.
     */
    int fanSpinupTimeMs;

    /** What is the minimum temperature that the fan should be allowed to run at? */
    double tempMinC;
    /** What is the temperature at which the fan should be running as fast as possible */
    double tempMaxC;

    /**
     * Once the fan is on, how far below the minimum temperature should we wait
     * before turning it off?
     *
     * The goal of this variable is to avoid turning the fan on and off repeatedly
     */
    double tempHysteresisC;
} Config;

typedef struct {
    enum ProcessState state;
    uint32_t lastChangeTimeMs;
    double lastFilteredTempC;
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
static const PtcThermistorConfig PTC_THERMISTOR_10K_3950 = {
    .nominalOhms = 10000,
    .nominalTempK = 25 + KELVIN_OFFSET,
    .beta = 3950,
};

double filterReadings(double newReading, double lastReading);

double resistanceToTempC(double thermistorOhms, const PtcThermistorConfig *config);

double ratioToUnknownBridgeResistance(double voltageRatio, double knownResistance);

double tempCountsToC(uint32_t tempCounts, const PtcThermistorConfig *config);

double fanVoltageRatio(double newTempC, uint32_t currentMs, const Config *config, State *state);

double ratioToDcmBuckDutyCycle(double voltageRatio);


#endif//FIRMWARE_LOGIC_H
