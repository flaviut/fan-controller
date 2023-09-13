#include "logic.h"
#include <assert.h>
#include <math.h>

double countsToRatio(uint32_t counts) {
    double result = (counts & 0xfff) / 4096.0;
    double clampedResult = fmin(1.0, fmax(1e-4, result));
    return clampedResult;
}

static const double REFERENCE_OHMS = 100000.0;

/**
 * @param voltageRatio ratio of the voltage to full-scale. For example, 0.5 for 2.5V on a 5V scale.
 * @param knownResistance the resistance of the R2 resistor in the voltage divider (ohms)
 * @return the resistance of the unknown resistor, R1, in ohms
 */
double ratioToUnknownBridgeResistance(double voltageRatio, double knownResistance) {
    // voltage cancels out, we just use the ratio directly to calculate the resistance
    assert(voltageRatio > 1e-5 && voltageRatio <= 1.0);
    double result = knownResistance * (1.0 / voltageRatio - 1.0);
    assert(result >= 0.0 && result <= 1e9);
    return result;
}

double resistanceToTempC(double thermistorOhms, const PtcThermistorConfig *config) {
    assert(config->nominalOhms > 0.0);
    assert(config->nominalTempK > 0.0);
    assert(config->beta > 0.0);

    double nominalOhms = (double) config->nominalOhms;
    double nominalTempK = (double) config->nominalTempK;
    double beta = (double) config->beta;


    // https://en.wikipedia.org/wiki/Thermistor#B_or_%CE%B2_parameter_equation
    double invTempK = (1.0 / nominalTempK) +
                      (1.0 / beta) * log(thermistorOhms / nominalOhms);
    return (int) ((1.0 / invTempK) - ((double) KELVIN_OFFSET));
}

double tempCountsToC(uint32_t tempCounts, const PtcThermistorConfig *config) {
    double voltageRatio = countsToRatio(tempCounts);
    double thermistorOhms = ratioToUnknownBridgeResistance(voltageRatio, REFERENCE_OHMS);
    return resistanceToTempC(thermistorOhms, config);
}

/**
 * Low-pass filter to eliminate noise & jitter in the temperature readings.
 */
double filterReadings(double newValue, double oldValue) {
    static const double PI = 3.14159265358979323846;
    static const double SAMPLING_RATE_HZ = 100.0;
    static const double CUTOFF_FREQ_HZ = 0.1;
    const double ALPHA = 1.0 - (1.0 / (1.0 + tan(PI * CUTOFF_FREQ_HZ / SAMPLING_RATE_HZ)));
    return ALPHA * newValue + (1.0 - ALPHA) * oldValue;
}

double clampd(double value, double min, double max) {
    if (value < min) {
        return min;
    } else if (value > max) {
        return max;
    }
    return value;
}

/** Linear interpolation between two points */
double interpolate(double x, double x0, double x1, double y0, double y1) {
    double xClamped = clampd(x, x0, x1);
    double xRange = x1 - x0;
    double yRange = y1 - y0;
    double xRatio = (xClamped - x0) / xRange;
    return y0 + xRatio * yRange;
}

void transitionState(State *state, enum ProcessState newState, uint32_t currentMs) {
    state->state = newState;
    state->lastChangeTimeMs = currentMs;
}

/**
 * this device effectively acts as a buck converter in discontinuous mode. Discontinuous mode
 * is much more complicated to analyze than continuous mode, and we need to convert the intended
 * output voltage to a duty cycle.
 *
 * the equation here varies depending on the load and the input voltage, but I've graphed the
 * theoretical output for several different input voltages (12V & 24V) and loads (0.1A, 0.2A, 0.3A),
 * and the results are fairly close the various parameters we expect this to be used with.
 *
 * We use 12V & 0.2A as the default parameters, since this is the most common use case.
 */
double ratioToDcmBuckDutyCycle(double voltageRatio) {
    static const double INPUT_VOLTAGE = 12.0;
    static const double INDUCTOR_VALUE = 47e-6;
    static const double OUTPUT_CURRENT = 0.2;
    static const double SWITCHING_FREQUENCY = PWM_FREQ_HZ;
    static const double SWITCHING_PERIOD = 1.0 / SWITCHING_FREQUENCY;

    voltageRatio = clampd(voltageRatio, 0.0, 1.0);

    // https://en.wikipedia.org/wiki/Buck_converter#Discontinuous_mode
    // solved for duty cycle: D = (sqrt(2) sqrt(Vo) sqrt(L) sqrt(Io))/sqrt(Vi^2 T - Vi Vo T)
    double outputVoltage = voltageRatio * INPUT_VOLTAGE;
    double top = sqrt(2.0) * sqrt(outputVoltage) * sqrt(INDUCTOR_VALUE) *
                 sqrt(OUTPUT_CURRENT);
    double bottom = sqrt(INPUT_VOLTAGE * INPUT_VOLTAGE * SWITCHING_PERIOD -
                         INPUT_VOLTAGE * outputVoltage * SWITCHING_PERIOD);
    double duty = top / bottom;
    // around .95 input the duty cycle exceeds 1.0, so clamp it
    return clampd(duty, 0.0, 1.0);
}

/**
 * Gets the output:input voltage ratio, based on the new temperature reading.
 *
 * Different from the duty cycle because we effectively have a buck converter acting in
 * DCM (discontinuous conduction mode), and the math there is a bit more complicated.
 */
double fanVoltageRatio(double newTempC, uint32_t currentMs, const Config *config, State *state) {
    double tempC = state->lastFilteredTempC = filterReadings(newTempC, state->lastFilteredTempC);
    switch (state->state) {
        case FAN_OFF: {
            if (tempC >= config->tempMinC) {
                // fan should be turned on
                transitionState(state, FAN_SPINUP, currentMs);
                // fall-through
                goto fan_spinup;
            } else {
                // fan should remain off
                return 0;
            }
        }
        case FAN_SPINUP:
        fan_spinup : {
            uint32_t elapsedMs = currentMs - state->lastChangeTimeMs;
            if (elapsedMs < config->fanSpinupTimeMs) {
                // fan is still spinning up, so keep the duty cycle at the spinup value
                return config->fanSpinupDutyCycle;
            } else {
                // fan has finished spinning up, so transition to the normal operating state
                transitionState(state, FAN_ON, currentMs);
                // fall through to the FAN_ON case (fan has finished spinning up)
                goto fan_on;
            }
        }
        case FAN_ON:
        fan_on : {
            if (tempC < (config->tempMinC - config->tempHysteresisC)) {
                // fan should be turned off
                transitionState(state, FAN_OFF, currentMs);
                return 0.0;
            } else {
                // interpolate between the min and max duty cycles based on the current temperature
                return interpolate(tempC, config->tempMinC, config->tempMaxC,
                                   config->fanMinDutyCycle, config->fanMaxDutyCycle);
            }
        }
        default:
            assert(0);
    }
}
