// 실행: g++ -std=c++17 -I src test/step_heading_test.cpp -o /tmp/sh && /tmp/sh
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "core/step_detector.h"

static float diff(float a, float b) { return fabsf(fmodf(a - b + 540.0f, 360.0f) - 180.0f); }

int main() {
  chh::StepHeading h;
  assert(h.onStep(42) == 42);  // 샘플 없으면 fallback
  // 350~10° 사이로 좌우 흔들림 → 평균은 0° 근처 (360 경계)
  for (int step = 0; step < 4; step++) {
    for (int i = 0; i < 25; i++) h.add(step % 2 ? 350 + i * 0.8f : 10 - i * 0.8f);
    float d = h.onStep(-1);
    if (step) assert(diff(d, 0) < 1.0f);
  }
  // 90° 회전 후 2걸음이 지나면 새 방향으로 수렴
  for (int step = 0; step < 2; step++) {
    for (int i = 0; i < 25; i++) h.add(90);
    float d = h.onStep(-1);
    assert(step == 0 ? (d > 30 && d < 60) : diff(d, 90) < 0.1f);
  }
  printf("ALL PASSED\n");
}
