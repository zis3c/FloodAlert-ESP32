#include <unity.h>

#include <flood_logic.h>

void setUp(void) {}

void tearDown(void) {}

void test_water_level_rejects_invalid_distance(void) {
  TEST_ASSERT_EQUAL_FLOAT(floodlogic::kInvalidReading,
                          floodlogic::waterLevelPercent(-1.0f, 100.0f));
}

void test_water_level_clamps_to_range(void) {
  TEST_ASSERT_EQUAL_FLOAT(0.0f, floodlogic::waterLevelPercent(100.0f, 100.0f));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, floodlogic::waterLevelPercent(150.0f, 100.0f));
  TEST_ASSERT_EQUAL_FLOAT(50.0f, floodlogic::waterLevelPercent(50.0f, 100.0f));
  TEST_ASSERT_EQUAL_FLOAT(100.0f, floodlogic::waterLevelPercent(0.0f, 100.0f));
}

void test_flood_alert_latch_has_hysteresis(void) {
  TEST_ASSERT_FALSE(floodlogic::nextFloodAlert(false, 0.99f, 1.0f, 0.0f));
  TEST_ASSERT_TRUE(floodlogic::nextFloodAlert(false, 1.0f, 1.0f, 0.0f));
  TEST_ASSERT_TRUE(floodlogic::nextFloodAlert(true, 0.5f, 1.0f, 0.0f));
  TEST_ASSERT_FALSE(floodlogic::nextFloodAlert(true, 0.0f, 1.0f, 0.0f));
  TEST_ASSERT_TRUE(floodlogic::nextFloodAlert(true, -1.0f, 1.0f, 0.0f));
}

void test_rain_debounce_needs_consecutive_reads(void) {
  floodlogic::RainDebounce debounce;
  debounce.update(true, 3);
  debounce.update(true, 3);
  TEST_ASSERT_FALSE(debounce.latched());

  debounce.update(true, 3);
  TEST_ASSERT_TRUE(debounce.latched());
}

void test_rain_debounce_resets_partial_streak(void) {
  floodlogic::RainDebounce debounce;
  debounce.update(true, 3);
  debounce.update(true, 3);
  debounce.update(false, 3);
  debounce.update(true, 3);
  debounce.update(true, 3);
  TEST_ASSERT_FALSE(debounce.latched());

  debounce.update(true, 3);
  TEST_ASSERT_TRUE(debounce.latched());
}

void test_rain_debounce_clears_after_dry_reads(void) {
  floodlogic::RainDebounce debounce;
  debounce.update(true, 3);
  debounce.update(true, 3);
  debounce.update(true, 3);
  TEST_ASSERT_TRUE(debounce.latched());

  debounce.update(false, 3);
  debounce.update(false, 3);
  TEST_ASSERT_TRUE(debounce.latched());

  debounce.update(false, 3);
  TEST_ASSERT_FALSE(debounce.latched());
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_water_level_rejects_invalid_distance);
  RUN_TEST(test_water_level_clamps_to_range);
  RUN_TEST(test_flood_alert_latch_has_hysteresis);
  RUN_TEST(test_rain_debounce_needs_consecutive_reads);
  RUN_TEST(test_rain_debounce_resets_partial_streak);
  RUN_TEST(test_rain_debounce_clears_after_dry_reads);
  return UNITY_END();
}
