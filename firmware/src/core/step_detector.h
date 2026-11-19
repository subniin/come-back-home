// 간단한 걸음 검출기 (가속도 크기 피크 검출)
//  - 50~100Hz로 update() 호출. 걸음이면 true
//  - 센서 담당이 더 좋은 검출기를 만들면 그걸 쓰고 Controller::onStep()만 호출하면 됨
#pragma once
#include <math.h>
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

// 걸음 헤딩 평균
//  - 허리는 걸을 때 좌우로 흔들려 걸음 순간의 헤딩 한 값은 튄다
//  - 헤딩 샘플을 add()로 계속 넣고, 걸음이 검출되면 onStep()이 직전 2걸음(보행 1주기) 동안의 원형 평균을 돌려준다
class StepHeading {
 public:
  void add(float deg) {
    float r = deg * kRad;
    s_[1] += sinf(r);
    c_[1] += cosf(r);
  }
  float onStep(float fallbackDeg) {
    float s = s_[0] + s_[1], c = c_[0] + c_[1];
    s_[0] = s_[1]; c_[0] = c_[1];
    s_[1] = c_[1] = 0;
    if (s * s + c * c < 1e-6f) return fallbackDeg;
    float d = atan2f(s, c) / kRad;
    return d < 0 ? d + 360.0f : d;
  }

 private:
  static constexpr float kRad = 0.01745329252f;
  float s_[2] = {0, 0}, c_[2] = {0, 0};  // [0] 직전 걸음 구간, [1] 지금 걸음 구간
};

}  // namespace chh
