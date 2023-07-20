#include "logic.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>

float countsToRatio(uint32_t counts) {
    return (float) counts / 4096.0f;
}

static const float REFERENCE_OHMS = 100000.0f;

/**
 * @param voltageRatio ratio of the voltage to full-scale. For example, 0.5f for 2.5V on a 5V scale.
 * @param knownResistance the resistance of the R2 resistor in the voltage divider (ohms)
 * @return the resistance of the unknown resistor, R1, in ohms
 */
float ratioToUnknownBridgeResistance(float voltageRatio, float knownResistance) {
    // voltage cancels out, we just use the ratio directly to calculate the resistance
    assert(voltageRatio >= 0.0f && voltageRatio <= 1.0f);
    return knownResistance * (1.0f / voltageRatio - 1.0f);
}

int resistanceToTempC(float thermistorOhms, const PtcThermistorConfig *config) {
    assert(config->nominalOhms > 0.0f);
    assert(config->nominalTempK > 0.0f);
    assert(config->beta > 0.0f);

    float nominalOhms = (float) config->nominalOhms;
    float nominalTempK = (float) config->nominalTempK;
    float beta = (float) config->beta;


    // https://en.wikipedia.org/wiki/Thermistor#B_or_%CE%B2_parameter_equation
    float invTempK = (1.0f / nominalTempK) +
                     (1.0f / beta) * logf(thermistorOhms / nominalOhms);
    return (int) ((1.0f / invTempK) - ((float) KELVIN_OFFSET));
}

int tempCountsToC(uint32_t tempCounts, const PtcThermistorConfig *config) {
    float voltageRatio = countsToRatio(tempCounts);
    float thermistorOhms = ratioToUnknownBridgeResistance(voltageRatio, REFERENCE_OHMS);
    return resistanceToTempC(thermistorOhms, config);
}

/**
 * Low-pass filter to eliminate noise & jitter in the temperature readings.
 *
 * -3dB @ 0.3Hz, assuming 10Hz sampling rate
 */
float filterReadings(float newValue, float oldValue) {
    static const float ALPHA = 0.2f;
    return ALPHA * newValue + (1.0f - ALPHA) * oldValue;
}

int clampi(int value, int min, int max) {
    if (value < min) {
        return min;
    } else if (value > max) {
        return max;
    }
    return value;
}

/**
 * Gets the current fan duty cycle, based on the new temperature reading.
 */
float dutyCycle(int newTempC, uint32_t currentMs, const Config *config, State *state) {
    state->lastFilteredTempC = filterReadings((float) newTempC, state->lastFilteredTempC);
    int tempC = (int) state->lastFilteredTempC;
    switch (state->state) {
        case FAN_OFF: {
            if (tempC >= config->tempMinC) {
                // fan should be turned on
                state->state = FAN_SPINUP;
                state->lastChangeTimeMs = currentMs;
                // fall-through
            } else {
                // fan should remain off
                return 0;
            }
        }
        case FAN_SPINUP: {
            uint32_t elapsedMs = currentMs - state->lastChangeTimeMs;
            if (elapsedMs < config->fanSpinupTimeMs) {
                // fan is still spinning up, so keep the duty cycle at the spinup value
                return config->fanSpinupDutyCycle;
            } else {
                // fan has finished spinning up, so transition to the normal operating state
                state->state = FAN_ON;
                state->lastChangeTimeMs = currentMs;
                // fall through to the FAN_ON case (fan has finished spinning up)
            }
        }
        case FAN_ON: {
            if (tempC < (config->tempMinC - config->tempHysteresisC)) {
                // fan should be turned off
                state->state = FAN_OFF;
                state->lastChangeTimeMs = currentMs;
                return 0;
            } else {
                // interpolate between the min and max duty cycles based on the current temperature
                int clampedTempC = clampi(tempC, config->tempMinC, config->tempMaxC);
                int tempRange = config->tempMaxC - config->tempMinC;
                float dutyCycleRange = config->fanMaxDutyCycle - config->fanMinDutyCycle;
                float tempRatio = (float) (clampedTempC - config->tempMinC) / (float) tempRange;
                return config->fanMinDutyCycle + tempRatio * dutyCycleRange;
            }
        }
        default:
            assert(0);
    }
}
