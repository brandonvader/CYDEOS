// Retargeted at env:cyd — CYD-Voice-Recorder's equivalent test ran against
// its esp32dev_oled env, which CYDEOS doesn't carry forward.
#include <Arduino.h>
#include <unity.h>

void test_basic_math(void) {
  TEST_ASSERT_EQUAL(4, 2 + 2);
}

void setup() {
  delay(2000);
  UNITY_BEGIN();
  RUN_TEST(test_basic_math);
  UNITY_END();
}

void loop() {}
