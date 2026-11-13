#pragma once

#include <cstdint>

namespace floodlogic {

constexpr float kInvalidReading = -1.0f;

float clampFloat(float value, float minValue, float maxValue);
float waterLevelPercent(float distanceCm, float tankDepthCm);
bool nextFloodAlert(bool latched, float waterPercent, float enterPercent, float exitPercent);

class RainDebounce {
 public:
  void update(bool raw, uint8_t threshold);
  bool latched() const;

 private:
  uint8_t lowStreak_ = 0;
  uint8_t highStreak_ = 0;
  bool latched_ = false;
};

}
