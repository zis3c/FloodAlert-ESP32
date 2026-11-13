#include "flood_logic.h"

namespace floodlogic {

float clampFloat(float value, float minValue, float maxValue) {
  if (value < minValue) {
    return minValue;
  }
  if (value > maxValue) {
    return maxValue;
  }
  return value;
}

float waterLevelPercent(float distanceCm, float tankDepthCm) {
  if (distanceCm < 0.0f) {
    return kInvalidReading;
  }

  const float waterDepth = tankDepthCm - distanceCm;
  return clampFloat((waterDepth / tankDepthCm) * 100.0f, 0.0f, 100.0f);
}

bool nextFloodAlert(bool latched, float waterPercent, float enterPercent, float exitPercent) {
  if (latched) {
    if (waterPercent >= 0.0f && waterPercent <= exitPercent) {
      return false;
    }
  } else if (waterPercent >= enterPercent) {
    return true;
  }
  return latched;
}

void RainDebounce::update(bool raw, uint8_t threshold) {
  if (raw) {
    if (lowStreak_ < 255) {
      ++lowStreak_;
    }
    highStreak_ = 0;
  } else {
    if (highStreak_ < 255) {
      ++highStreak_;
    }
    lowStreak_ = 0;
  }

  if (lowStreak_ >= threshold) {
    latched_ = true;
  } else if (highStreak_ >= threshold) {
    latched_ = false;
  }
}

bool RainDebounce::latched() const {
  return latched_;
}

}
