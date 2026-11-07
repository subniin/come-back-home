// 간단한 걸음 검출기 (가속도 크기 피크 검출)
//  - 50~100Hz로 update() 호출. 걸음이면 true
//  - 센서 담당이 더 좋은 검출기를 만들면 그걸 쓰고 Controller::onStep()만 호출하면 됨
#pragma once
#include <stdint.h>
#include "config.h"

namespace chh {

class StepDetector {
 public:
  bool update(float accMagG, uint32_t nowMs) {
    lp_ += 0.25f * (accMagG - lp_);  // 저역통과
    if (!high_) {
      if (lp_ > cfg::STEP_HI_G && nowMs - last_ >= cfg::STEP_MIN_MS) {
        high_ = true;
        last_ = nowMs;
        return true;
      }
    } else if (lp_ < cfg::STEP_LO_G) {
      high_ = false;
    }
    return false;
  }

 private:
  float lp_ = 1.0f;
  bool high_ = false;
  uint32_t last_ = 0;
};

}  // namespace chh
