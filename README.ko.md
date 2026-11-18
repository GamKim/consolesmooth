# ConsoleSmooth

[English](README.md)

AVerMedia Streaming Center용 **비공식·무료 프레임 생성 플러그인**입니다. 캡처보드로 들어온 콘솔 화면(스위치 2, PS5 등, 보통 60fps)에 중간 프레임을 만들어 넣어 120·160fps로 더 부드럽게 보여 줍니다. 생성된 프레임을 포함해 녹화할 수도 있습니다.

AVerMedia와 관계없는 개인 프로젝트이며, AVerMedia의 프로그램이나 파일은 포함하지 않습니다.

**처음 쓰시나요? [사용법](docs/GUIDE.ko.md)을 따라 해 보세요** (설치, 처음 켜기, 설정, 문제 해결).

## 1.1.0에서 바뀐 점

- **저지연 보기 정식 기능**: 생성을 켠 화면이 원본(패스스루)보다 **약 9~19ms**만 늦게 나옵니다. 1.0.1은 77~87ms였습니다(RIFE·FSR 3 기준). 생성을 끈 Streaming Center 기본 미리보기(약 32ms)보다도 빠릅니다.
- **모델 6개**: RIFE 4.6, RIFE 4.15 lite, FSR 3, AMD FSR 프레임 생성, NVIDIA Video Frame Generation, NVIDIA DLSS 프레임 생성.
- **권장 설정**이 그래픽카드 속도를 재서 생성 버퍼를 15~25ms 사이에서 고릅니다.

## 특징

- **Streaming Center 안에서 바로 동작**: 별도 창 캡처나 화면 덮기 없이, 툴바 버튼이나 단축키(기본 Ctrl+Alt+F, 고급 탭에서 바꿀 수 있음)로 켜고 끕니다. 전체 화면에서는 Esc로 닫으면서 끕니다. 설정 창에서는 고르고 저장만 하며, 켤 때 적용됩니다.
- **저지연 보기**: 켜기 버튼·단축키를 누르면 전체 화면으로 바로 보여 줍니다. 저지연 보기 + 전체 화면 + 2배일 때는 그래픽카드 안에서 바로 만들어 바로 내보내는 빠른 방식으로 동작합니다. 켜 두면 녹화·방송에는 화면이 들어가지 않습니다.
  - 빠른 방식은 Streaming Center를 켤 때 정해집니다. 모델이나 화면 설정을 바꾸면 설정 창이 다시 시작하라고 알려 줍니다.
  - 이 PC에서 빠른 방식이 안 되면 다음 시작부터 자동으로 기존 방식을 씁니다.
- **생성 프레임 녹화**: 녹화 소스를 "생성 프레임 포함"으로 두면 출력 fps(예: 120/160fps) 파일로 저장됩니다(저지연 보기를 끈 상태). 30fps 게임은 파일이 120fps여도 새 화면은 게임 fps × 배수(2배면 60장)입니다.
- **화면 정보(HUD) 보호**: 체력바·지도·버튼 표시(반투명 포함)처럼 고정된 화면 요소를 원본 그대로 써서 떨림을 줄입니다.
- **30fps 보정**: 30fps 게임을 "게임 fps × 배수"로 고르게 보여 줍니다(2배 → 30 → 60, 16.7ms마다). 지연 약 +8ms.
- **화면 숫자**: 기본은 `입력 / 지금 fps`(캡처 신호 / 1초에 화면에 낸 장, 예: 60 / 120).
- **화면 동기화**: 수직 동기화(기본) / 가변 주사율(G-Sync·FreeSync) / 끔 중에서 고릅니다.
- **59.94Hz 입력(PS5) 지원**, **권장 설정**(실제로 시험 생성하고, 그래픽카드와 모니터의 가변 주사율 지원을 보고 추천), 한국어/영어.

## 모델

| 모델 | 특징 | 그래픽카드 |
|---|---|---|
| RIFE 4.6 (기본) | 화질과 속도의 균형 | Vulkan 지원 GPU |
| RIFE 4.15 lite | RIFE 4.6보다 화질이 조금 좋고 조금 무거움 | Vulkan 지원 GPU |
| FSR 3 | 가볍고 2배·4배 지원 | Vulkan 1.3 GPU |
| AMD FSR 프레임 생성 | AMD 공식 프레임 생성 DLL, 가장 가벼움(2배) | DirectX 12 GPU |
| NVIDIA Video Frame Generation | 화질 가장 좋음. 처음 고를 때 NVIDIA에서 런타임을 받습니다 | NVIDIA RTX 40/50 |
| NVIDIA DLSS 프레임 생성 | 가벼움(2·3·4배, 3·4배는 RTX 50) | NVIDIA RTX 40/50 |

같은 30fps 입력으로 만든 60fps를 실제 60fps 녹화와 비교한 점수(높을수록 좋음, 스위치 2 젤다 밤 숲 701장): NVIDIA Video Frame Generation 33.22dB, RIFE 4.15 lite 33.05, RIFE 4.6 32.94, DLSS 프레임 생성 32.64, FSR 3 32.55, AMD FSR 프레임 생성 31.04.

## 필요한 것

- AVerMedia 캡처보드 + Streaming Center 1.8.22.2 · 최신 1.9.19.1 x64 (확인: Live Gamer 4K GC573, GC313 Pro, GC553 Pro)
- Windows 10/11 x64
- 그래픽카드: 위 모델 표 참고. 최소 사양은 **AMD Radeon RX 6600**(FHD 60 → 120fps, 기존 방식으로 확인). 지포스는 FSR 3 기준 RTX 2060급, RIFE 기준 RTX 3070·4060급 이상(근사 측정).
- 120Hz 이상 모니터
- 120fps 녹화는 Streaming Center 녹화 비트레이트를 40\~50Mbps로 올리세요.

## 설치

[Releases](https://github.com/GamKim/consolesmooth/releases)에서 `ConsoleSmooth-Plugin-Setup-1.1.0.exe`를 받아 실행합니다. 서명이 없는 설치 파일이라 SmartScreen 경고가 나오면 "추가 정보 → 실행"을 누릅니다. 그림으로 보는 사용법: [docs/GUIDE.ko.md](docs/GUIDE.ko.md). 모든 설정 설명: [installer/INSTALL-README.txt](installer/INSTALL-README.txt).

## 제거

Streaming Center를 닫고 `Plugin\ConsoleSmooth` 폴더의 `Uninstall.exe`를 실행해 **제거**를 누릅니다(기본 위치: `C:\Program Files (x86)\AVerMedia\AVerMedia Streaming Center\Plugin\ConsoleSmooth`). Streaming Center 원본과 설정은 남습니다. 자세한 내용은 [installer/UNINSTALL-README.txt](installer/UNINSTALL-README.txt)에 있습니다.

## 측정값 (RTX 5080, GC573, FHD, 2026-11-17~18)

### 지연 (원본 패스스루 화면보다 늦게 나오는 시간)

휴대폰 240fps로 원본 모니터와 PC 화면을 함께 찍고, 칼을 휘두를 때 생기는 궤적이 두 화면에 뜨는 시점 차이를 잰 값입니다(스위치 2 젤다, 휘두름 3번 평균).

| 화면 | 생성 | 지연 |
|---|---|---|
| ConsoleSmooth 1.1.0 저지연 보기 · FSR 3 | 켬 | 8.8ms |
| ConsoleSmooth 1.1.0 저지연 보기 · RIFE 4.6 | 켬 | 11.8ms |
| ConsoleSmooth 1.1.0 저지연 보기 · AMD FSR 프레임 생성 | 켬 | 12.5ms |
| ConsoleSmooth 1.1.0 저지연 보기 · NVIDIA Video Frame Generation | 켬 | 12.6ms |
| ConsoleSmooth 1.1.0 저지연 보기 · RIFE 4.15 lite | 켬 | 12.7ms |
| ConsoleSmooth 1.1.0 저지연 보기 · NVIDIA DLSS 프레임 생성 | 켬 | 19.0ms |
| Streaming Center 기본 미리보기 | 끔 | 31.9ms |
| ConsoleSmooth 1.0.1 저지연 보기 · RIFE 4.6 / FSR 3 | 켬 | 83.0 / 76.7ms |

원본 직결(패스스루)보다는 늦게 나오므로, 지연에 아주 민감한 대전·리듬 게임에서는 패스스루 화면을 쓰세요.

### 비교 영상

유튜브(4K, 그림을 누르면 열립니다):

**지연 비교** (휴대폰 240fps, 8배 느리게. 작은 화면 = 원본 패스스루, 큰 화면 = PC)

| 생성 켠 화면: ConsoleSmooth · Veyra · Lossless Scaling | 1.0.1 vs 1.1.0 |
|---|---|
| [![지연 비교 1](https://img.youtube.com/vi/kwYfBRFG5Do/hqdefault.jpg)](https://youtu.be/kwYfBRFG5Do) | [![지연 비교 2](https://img.youtube.com/vi/GAScSgJt1fw/hqdefault.jpg)](https://youtu.be/GAScSgJt1fw) |
| [1.1.0 나머지 모델 4개](https://youtu.be/zf4zC5MGOJ8) | [생성 켠 ConsoleSmooth vs 생성 없는 미리보기 3종](https://youtu.be/AzRhoV9cnZs) |

**화질 비교** (같은 30fps 입력으로 만든 60fps, 칸마다 1920×1080 원래 크기)

| 실제 60fps vs RIFE 4.6 · RIFE 4.15 lite · NVIDIA Video Frame Generation | ConsoleSmooth vs Veyra · NVIDIA 기술 |
|---|---|
| [![화질 비교 1](https://img.youtube.com/vi/881Qw9zsiYA/hqdefault.jpg)](https://youtu.be/881Qw9zsiYA) | [![화질 비교 2](https://img.youtube.com/vi/JvTUElCef24/hqdefault.jpg)](https://youtu.be/JvTUElCef24) |
| [만든 프레임만 반속도](https://youtu.be/dBSYabFpO98) | [만든 프레임만 반속도](https://youtu.be/TdxeY9D8SIE) |

[ConsoleSmooth vs Veyra · FSR 3 · FSR 3.1 · AMD FSR 프레임 생성](https://youtu.be/Ph15SAfiZmE) ([만든 프레임만 반속도](https://youtu.be/0ZJzv03232M))

다른 영상과 설정 창 사진은 [media](media/README.ko.md)에 있습니다.

### 생성 비율

| 입력 | 설정 | 생성 비율 |
|---|---|---|
| 스위치 2 (60fps) | 저지연 보기 · 6개 모델 2배 → 120fps | 10초마다 600장 중 597~600장 |
| 스위치 2 (60fps) | RIFE 목표 160fps | 98% |
| PS5 GTA5 (59.94Hz) | RIFE 2배 / RIFE 160 / FSR 3 120 | 96.4% / 91.6% / 100% |
| 스위치 2 (RX 6600) | RIFE 2배(1/4 크기 계산) / FSR 3 | 99.8% / 98% |

## 한계

- 저지연 보기의 빠른 방식은 2배일 때만 쓰입니다. 3배·4배, 목표 프레임, 30fps 보정, 안정 우선, 그래픽카드가 두 개인 노트북에서는 기존 방식(생성 버퍼 15~25ms)으로 동작합니다.
- 빠른 방식은 RTX 5080에서만 시험했습니다. 다른 그래픽카드에서 문제가 있으면 [Issues](https://github.com/GamKim/consolesmooth/issues)에 알려 주세요.
- 아주 빠른 카메라 회전에서 배경이 조금 일렁일 수 있습니다. 30fps 게임은 고개를 갑자기 돌릴 때 얼굴이 일렁일 수 있습니다.
- 3인칭 게임에서 캐릭터가 오래 서 있으면 화면 정보 보호가 캐릭터를 화면 정보로 볼 수 있습니다(머리카락 끝 깨짐 → 화면 정보 보호 끄기).
- HDR·10비트, 8K는 지원하지 않습니다. 입력은 30\~120fps(29.97·59.94·119.88 포함)를 받습니다.
- Streaming Center 버전이 바뀌면 다시 확인이 필요할 수 있습니다.

## 구조

| 폴더 | 내용 |
|---|---|
| `engine/` | 프레임 생성 엔진 DLL(C++17): 실시간 스케줄러, 모델 연결(RIFE·FSR 3·AMD·NVIDIA), 그래픽카드 공유 텍스처 경로, 화면 정보 보호, 검사 프로그램(`checks/`) |
| `plugin/` | Streaming Center 플러그인(C, GStreamer tracer): 미리보기 연결, 설정 창, 입력 감지, 전체 화면·저지연 보기 |
| `installer/` | 설치 프로그램(C#, .NET Framework 4)과 설치 시험 |
| `third_party/` | rife-ncnn-vulkan(ncnn·glslang 포함), FidelityFX SDK 1.1.4 일부, Vulkan-Headers. 바꾼 곳은 `third_party/PATCHES/` |
| `tools/` | 빌드 도구 받기(`Get-Toolchain.ps1`), 한 번에 빌드(`Build-All.ps1`) |

AMD FSR 프레임 생성과 NVIDIA DLSS 프레임 생성은 각 회사 SDK로 만든 작은 연결 DLL(`cs_amdfg.dll`, `cs_dlssg.dll`)을 설치 파일에 넣어 씁니다. NVIDIA DLSS 런타임(`nvngx_dlssg.dll`)과 AMD 서명 DLL은 설치 파일에만 들어 있고, 이 저장소에는 없습니다.

## 문제 신고

설정 창의 **진단 정보 내보내기** 파일을 [Issues](https://github.com/GamKim/consolesmooth/issues)에 첨부해 주세요.

## 라이선스

MIT ([LICENSE](LICENSE)). 함께 쓰는 부품의 라이선스: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). This software uses NVIDIA DLSS. NVIDIA, the NVIDIA logo and DLSS are trademarks of NVIDIA Corporation.
