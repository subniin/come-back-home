// PDR 백트래킹 자가 테스트 (알고리즘만 확인)
//  - 센서·모터·앱 없이, 미리 정한 걸음 데이터(변수)를 경로 기록 → 복귀 → 차단 알고리즘에 넣고 결과만 출력
//  - 시리얼 모니터에서 SELFTEST (전체) / SELFTEST 2 (2번만) / SELFTEST NOISE 10 (노이즈 바꾸기)
//  - 시나리오와 변수는 pdr_selftest.cpp 맨 위에서 고치면 됨
#pragma once
#include <stdint.h>

namespace chh {

typedef void (*LogFn)(const char* line);

// which: 0 = 전체, 1~N = 해당 시나리오만. 반환값 = 실패 개수
int runPdrSelfTest(LogFn log, int which);
int pdrSelfTestCount();
void pdrSelfTestSetNoise(float deg);
float pdrSelfTestNoise();

}  // namespace chh
