// 각도/좌표 계산 유틸
//  - 실내 평면 좌표: x = 오른쪽(동), y = 앞(북), 단위 m
//  - 각도: 0° = +y(정면/북), 시계방향 증가, 단위 degree
#pragma once
#include <math.h>
#include <stdint.h>

namespace chh {

constexpr double PI_D    = 3.14159265358979323846;
constexpr float  DEG2RAD = (float)(PI_D / 180.0);
constexpr float  RAD2DEG = (float)(180.0 / PI_D);

inline float wrap360(float a) {
  a = fmodf(a, 360.0f);
  if (a < 0) a += 360.0f;
  return a;
}
inline float wrap180(float a) {
  a = wrap360(a);
  return a > 180.0f ? a - 360.0f : a;
}
inline float dist2d(float x1, float y1, float x2, float y2) {
  return hypotf(x2 - x1, y2 - y1);
}
// (dx, dy) 벡터의 방위각
inline float planarBearing(float dx, float dy) {
  return wrap360(atan2f(dx, dy) * RAD2DEG);
}

// 상대각(목표방위 - 내 헤딩) → 허리 모터 번호(0~7)
//  0=정면(배꼽) 1=오른앞 2=오른쪽 3=오른뒤 4=뒤(등) 5=왼뒤 6=왼쪽 7=왼앞
//  prev: 직전 모터 번호(없으면 255). 경계 근처에서 hyst만큼 기존 값을 유지.
inline uint8_t sectorOf(float relDeg, uint8_t prev, float hystDeg) {
  relDeg = wrap360(relDeg);
  if (prev < 8) {
    float center = prev * 45.0f;
    if (fabsf(wrap180(relDeg - center)) <= 22.5f + hystDeg) return prev;
  }
  return (uint8_t)(((int)floorf((relDeg + 22.5f) / 45.0f)) % 8);
}

}  // namespace chh
