#include "logic.h"
#include "unity.h"

void test_resistanceToTempC(void) {
    TEST_ASSERT_EQUAL_INT(25, resistanceToTempC(10000., &PTC_THERMISTOR_10K_3950));
    TEST_ASSERT_INT_WITHIN(3, -30, resistanceToTempC(172478., &PTC_THERMISTOR_10K_3950));
    TEST_ASSERT_INT_WITHIN(3, 100, resistanceToTempC(650., &PTC_THERMISTOR_10K_3950));
}

void test_ratioToUnknownBridgeResistance(void) {
    TEST_ASSERT_FLOAT_WITHIN(10, 100000., ratioToUnknownBridgeResistance(0.5, 100000.));
    TEST_ASSERT_FLOAT_WITHIN(10, 300000., ratioToUnknownBridgeResistance(0.25, 100000.));
    TEST_ASSERT_FLOAT_WITHIN(10, 33333., ratioToUnknownBridgeResistance(0.75, 100000.));
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
    TEST_ASSERT_EQUAL(0, dutyCycle(25, 0, &config, &state));
    TEST_ASSERT_EQUAL(FAN_OFF, state.state);

    // temp rises, spin up for 1 second
    state.lastFilteredTempC = 35;
    TEST_ASSERT_EQUAL(1, dutyCycle(35, 100, &config, &state));
    TEST_ASSERT_EQUAL(FAN_SPINUP, state.state);
    TEST_ASSERT_EQUAL(100, state.lastChangeTimeMs);
    TEST_ASSERT_EQUAL(1, dutyCycle(35, 1000, &config, &state));
    TEST_ASSERT_EQUAL(FAN_SPINUP, state.state);

    // done spinning up, now to normal operation
    TEST_ASSERT_EQUAL(.37, dutyCycle(35, 1101, &config, &state));
    TEST_ASSERT_EQUAL(FAN_ON, state.state);

    // temperature increases, but filtered value changes more slowly
    dutyCycle(44, 1102, &config, &state);
    TEST_ASSERT_EQUAL_FLOAT(36.8f, state.lastFilteredTempC);

    // eventually the filtered value catches up
    for (int i = 0; i < 20; i++) { dutyCycle(44, 1103 + i, &config, &state); }
    TEST_ASSERT_FLOAT_WITHIN(0.2, 44.f, state.lastFilteredTempC);

    // temperature drops, but above the hysteresis
    state.lastFilteredTempC = 26;
    TEST_ASSERT_EQUAL(.30, dutyCycle(26, 1123, &config, &state));

    // temperature drops below the hysteresis & system turns off
    state.lastFilteredTempC = 24;
    TEST_ASSERT_EQUAL(0, dutyCycle(24, 1124, &config, &state));
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
    TEST_ASSERT_EQUAL(0, dutyCycle(25, 0, &config, &state));

    // temp rises, spin up for 1 second
    state.lastFilteredTempC = 35;
    TEST_ASSERT_EQUAL(1, dutyCycle(35, UINT32_MAX - 100, &config, &state));
    TEST_ASSERT_EQUAL(FAN_SPINUP, state.state);
    TEST_ASSERT_EQUAL(UINT32_MAX - 100, state.lastChangeTimeMs);
    TEST_ASSERT_EQUAL(1, dutyCycle(35, UINT32_MAX, &config, &state));
    TEST_ASSERT_EQUAL(UINT32_MAX - 100, state.lastChangeTimeMs);

    // wraparound, but still spinning up
    TEST_ASSERT_EQUAL(1, dutyCycle(35, 100, &config, &state));
    TEST_ASSERT_EQUAL(FAN_SPINUP, state.state);
    TEST_ASSERT_EQUAL(UINT32_MAX - 100, state.lastChangeTimeMs);

    // done spinning up, now to normal operation
    TEST_ASSERT_EQUAL(.37, dutyCycle(35, 901, &config, &state));
    TEST_ASSERT_EQUAL(FAN_ON, state.state);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_resistanceToTempC);
    RUN_TEST(test_ratioToUnknownBridgeResistance);
    RUN_TEST(test_dutyCycleStandard);
    RUN_TEST(test_wrapAroundTime);
    return UNITY_END();
}

void setUp(void) {}

void tearDown(void) {}
