#include "logic.h"
#include "unity.h"

void test_resistanceToTempC(void) {
    TEST_ASSERT_EQUAL_INT(25, resistanceToTempC(10000., &PTC_THERMISTOR_10K_3950));
    TEST_ASSERT_INT_WITHIN(3, 0, resistanceToTempC(31732., &PTC_THERMISTOR_10K_3950));
    TEST_ASSERT_INT_WITHIN(3, 75, resistanceToTempC(1480., &PTC_THERMISTOR_10K_3950));
}

void test_ratioToUnknownBridgeResistance(void) {
    TEST_ASSERT_DOUBLE_WITHIN(10, 100000., ratioToUnknownBridgeResistance(0.5, 100000.));
    TEST_ASSERT_DOUBLE_WITHIN(10, 300000., ratioToUnknownBridgeResistance(0.25, 100000.));
    TEST_ASSERT_DOUBLE_WITHIN(10, 33333., ratioToUnknownBridgeResistance(0.75, 100000.));
}

void test_dutyCycleStandard(void) {
    State state = {
        .state = FAN_OFF,
        .lastChangeTimeMs = 0,
        .lastFilteredTempC = 25,
    };
    Config config = {
        .fanSpinupDutyCycle = 1.f,
        .fanSpinupTimeMs = 1000,
        .tempMinC = 30,
        .tempMaxC = 80,
        .tempHysteresisC = 5,

        .fanMinDutyCycle = .3f,
        .fanMaxDutyCycle = 1.f,
    };

    // start off with fan off
    TEST_ASSERT_EQUAL(0, fanVoltageRatio(25, 0, &config, &state));
    TEST_ASSERT_EQUAL(FAN_OFF, state.state);

    // temp rises, spin up for 1 second
    state.lastFilteredTempC = 35;
    TEST_ASSERT_EQUAL(1, fanVoltageRatio(35, 100, &config, &state));
    TEST_ASSERT_EQUAL(FAN_SPINUP, state.state);
    TEST_ASSERT_EQUAL(100, state.lastChangeTimeMs);
    TEST_ASSERT_EQUAL(1, fanVoltageRatio(35, 1000, &config, &state));
    TEST_ASSERT_EQUAL(FAN_SPINUP, state.state);

    // done spinning up, now to normal operation
    TEST_ASSERT_EQUAL(.37, fanVoltageRatio(35, 1101, &config, &state));
    TEST_ASSERT_EQUAL(FAN_ON, state.state);

    // temperature increases, but filtered value changes more slowly
    fanVoltageRatio(44, 1102, &config, &state);
    TEST_ASSERT_DOUBLE_WITHIN(0.1, 35., state.lastFilteredTempC);

    // eventually the filtered value catches up
    for (int i = 0; i < 400; i++) { fanVoltageRatio(44, 1103 + i, &config, &state); }
    TEST_ASSERT_DOUBLE_WITHIN(0.5, 41., state.lastFilteredTempC);

    // temperature drops, but above the hysteresis
    state.lastFilteredTempC = 26;
    TEST_ASSERT_EQUAL(.30, fanVoltageRatio(26, 1123, &config, &state));

    // temperature drops below the hysteresis & system turns off
    state.lastFilteredTempC = 24;
    TEST_ASSERT_EQUAL(0, fanVoltageRatio(24, 1124, &config, &state));
    TEST_ASSERT_EQUAL(FAN_OFF, state.state);
}

void test_wrapAroundTime(void) {
    State state = {
        .state = FAN_OFF,
        .lastChangeTimeMs = 0,
        .lastFilteredTempC = 25,
    };
    Config config = {
        .fanSpinupDutyCycle = 1,
        .fanSpinupTimeMs = 1000,
        .tempMinC = 30,
        .tempMaxC = 80,
        .tempHysteresisC = 5,

        .fanMinDutyCycle = .3f,
        .fanMaxDutyCycle = 1.f,
    };

    // start off with fan off
    TEST_ASSERT_EQUAL(0, fanVoltageRatio(25, 0, &config, &state));

    // temp rises, spin up for 1 second
    state.lastFilteredTempC = 35;
    TEST_ASSERT_EQUAL(1, fanVoltageRatio(35, UINT32_MAX - 100, &config, &state));
    TEST_ASSERT_EQUAL(FAN_SPINUP, state.state);
    TEST_ASSERT_EQUAL(UINT32_MAX - 100, state.lastChangeTimeMs);
    TEST_ASSERT_EQUAL(1, fanVoltageRatio(35, UINT32_MAX, &config, &state));
    TEST_ASSERT_EQUAL(UINT32_MAX - 100, state.lastChangeTimeMs);

    // wraparound, but still spinning up
    TEST_ASSERT_EQUAL(1, fanVoltageRatio(35, 100, &config, &state));
    TEST_ASSERT_EQUAL(FAN_SPINUP, state.state);
    TEST_ASSERT_EQUAL(UINT32_MAX - 100, state.lastChangeTimeMs);

    // done spinning up, now to normal operation
    TEST_ASSERT_EQUAL(.37, fanVoltageRatio(35, 901, &config, &state));
    TEST_ASSERT_EQUAL(FAN_ON, state.state);
}

void test_filterReadings(void) {
    // should be no filtering on first reading for any temp between 0 and 150
    for (int i = 0; i < 150; i++) {
        // should be no filtering on first reading for any temp between 0 and 150
        TEST_ASSERT_EQUAL_DOUBLE(i, filterReadings(i, i));
    }
}

void test_tempCountsToC(void) {
    // make sure we handle boundary conditions correctly
    TEST_ASSERT_LESS_OR_EQUAL_DOUBLE(-100, tempCountsToC(0, &PTC_THERMISTOR_10K_3950));
    TEST_ASSERT_GREATER_OR_EQUAL_DOUBLE(150, tempCountsToC(0xfff, &PTC_THERMISTOR_10K_3950));

    // out-of-bounds inputs should not violate output invariant
    double maxIntTemp = tempCountsToC(UINT32_MAX, &PTC_THERMISTOR_10K_3950);
    TEST_ASSERT_GREATER_OR_EQUAL_DOUBLE(-100, maxIntTemp);
    TEST_ASSERT_LESS_OR_EQUAL_DOUBLE(300, maxIntTemp);
}

void test_spuriousReading(void) {
    // a way-out-of-range reading should not crash
    filterReadings(tempCountsToC(0xffff, &PTC_THERMISTOR_10K_3950), 35.);

    // a single spike of 2**12 should not result in a temperature change of > 1°C
    double temp = 35.0;
    temp = filterReadings(tempCountsToC(0xffff, &PTC_THERMISTOR_10K_3950), temp);
    TEST_ASSERT_LESS_OR_EQUAL_DOUBLE(36, temp);
}

void test_dcmBuckRatioToDutyCycle(void) {
    TEST_ASSERT_DOUBLE_WITHIN(0.05, 0.2, ratioToDcmBuckDutyCycle(0.5));
    TEST_ASSERT_DOUBLE_WITHIN(0.05, 0.1, ratioToDcmBuckDutyCycle(0.25));
    TEST_ASSERT_DOUBLE_WITHIN(0.05, 0.4, ratioToDcmBuckDutyCycle(0.75));
    TEST_ASSERT_DOUBLE_WITHIN(0.05, 0.9, ratioToDcmBuckDutyCycle(0.95));

    for (int i = 0; i < 1000; i++) {
        // output should not exceed [0, 1]
        double result = ratioToDcmBuckDutyCycle(i / 1000.0);
        if (result > 1 || result < 0) {
            // error message with the value that failed
            char msg[100];
            sprintf(msg, "Duty cycle should not exceed [0, 1], but was %f for input ratio %f",
                    result, i / 1000.);
            TEST_FAIL_MESSAGE(msg);
        }
    }
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_resistanceToTempC);
    RUN_TEST(test_ratioToUnknownBridgeResistance);
    RUN_TEST(test_dutyCycleStandard);
    RUN_TEST(test_wrapAroundTime);
    RUN_TEST(test_filterReadings);
    RUN_TEST(test_tempCountsToC);
    RUN_TEST(test_spuriousReading);
    RUN_TEST(test_dcmBuckRatioToDutyCycle);
    return UNITY_END();
}

void setUp(void) {}

void tearDown(void) {}
