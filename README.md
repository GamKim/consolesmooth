# ConsoleSmooth

[English](README.en.md)

AVerMedia Streaming Center용 **비공식·무료 프레임 생성 플러그인**입니다. 캡처보드로 들어온 콘솔 화면(스위치 2, PS5 등, 보통 60fps)에 중간 프레임을 만들어 넣어, Streaming Center 미리보기를 120·160fps로 더 부드럽게 보여 줍니다. 생성된 프레임을 포함해 녹화할 수도 있습니다.

AVerMedia와 관계없는 개인 프로젝트이며, AVerMedia의 프로그램이나 파일은 포함하지 않습니다.

## 특징

- **Streaming Center 안에서 바로 동작**: 별도 창 캡처나 화면 덮기 없이, 툴바 버튼으로 켜고 끕니다.
- **생성 프레임 녹화**: 녹화 소스를 "생성 프레임 포함"으로 두면 출력 fps(예: 120/160fps) 파일로 저장됩니다. 30fps 게임은 파일이 120fps여도 새 화면은 게임 fps × 배수(2배면 60장)입니다.
- **모델 2개**: RIFE 4.6(기본, 화질 우선), AMD FSR 3(2배·4배, 가벼움).
- **HUD 보호**: 체력바·지도·버튼 표시(반투명 포함)처럼 고정된 화면 요소를 원본 그대로 써서 떨림을 줄입니다.
- **30fps 보정**: 30fps 게임을 "게임 fps × 배수"로 고르게(2배 → 30 → 60, 16.7ms마다). 지연 약 +8ms.
- **화면 숫자**: 기본은 `입력 / 지금 fps`(캡처 신호 / 1초에 화면에 낸 장, 예: 60 / 120). "게임 실제 프레임으로 표시"를 켜면 `원본 / 지금`(게임이 바꾼 화면 / 실제로 보이는 새 화면, 예: 30fps 게임 2배는 30 / 60).
- **저지연 보기(베타)**: 전체 화면으로 들어온 화면을 바로 보여 줍니다(생성을 꺼도 동작). 켜 두면 녹화·방송에는 화면이 들어가지 않습니다. PS5 메뉴 화면, 휴대폰 240fps 측정(11-13): 패스스루보다 약 23ms 늦음(생성 끈 Streaming Center 기본 미리보기는 약 31~33ms).
- **59.94Hz 입력(PS5) 지원**, **권장 설정**(실제로 25초 시험 생성 후 추천), 한국어/영어.

## 필요한 것

- AVerMedia 캡처보드 + Streaming Center 1.8.22.2 · 최신 1.9.19.1 x64 (확인: Live Gamer 4K GC573, GC313 Pro, GC553 Pro)
- Windows x64, Vulkan GPU. 최소 사양: **AMD Radeon RX 6600** (FHD 60 → 120fps). 지포스는 RTX 2060급 이상(근사 측정).
- 120Hz 이상 모니터
- 120fps 녹화는 Streaming Center 녹화 비트레이트를 40\~50Mbps로 올리세요(15Mbps면 화면이 뭉개집니다).

## 설치

[Releases](https://github.com/GamKim/consolesmooth/releases)에서 `ConsoleSmooth-Plugin-Setup-1.0.1.exe`를 받아 실행합니다. 서명이 없는 설치 파일이라 SmartScreen 경고가 나오면 "추가 정보 → 실행"을 누릅니다. 자세한 사용법은 [installer/INSTALL-README.txt](installer/INSTALL-README.txt)에 있습니다.

## 제거

Streaming Center를 닫고 `Plugin\ConsoleSmooth` 폴더의 `Uninstall.exe`를 실행해 **제거**를 누릅니다(기본 위치: `C:\Program Files (x86)\AVerMedia\AVerMedia Streaming Center\Plugin\ConsoleSmooth`). Streaming Center 원본과 설정은 남습니다. 자세한 내용은 [installer/UNINSTALL-README.txt](installer/UNINSTALL-README.txt)에 있습니다.

## 측정값 (RTX 5080, GC573, FHD)

비교 영상(화질·지연·Lossless Scaling)과 설정 창 사진은 [media](media/README.md) 폴더에 있습니다.

| 입력 | 설정 | 생성 비율 |
|---|---|---|
| 스위치 2 (60fps) | RIFE 2배 → 120fps | 99% 이상 |
| 스위치 2 (60fps) | RIFE 목표 160fps | 98% |
| PS5 GTA5 (59.94Hz) | RIFE 2배 / RIFE 160 / FSR 3 120 | 96.4% / 91.6% / 100% |
| 스위치 2 (RX 6600) | RIFE 2배(1/4 크기 계산) / FSR 3 | 99.8% / 98% |
| PS5 GTA5 화질 모드 (30fps) | RIFE 2배 + 30fps 보정 | 새 화면 30 → 58\~60, 간격 16.7ms, 버퍼 33.4ms |

녹화 파일에서 실제로 바뀐 화면을 센 값 (2026-11-11, 여러 번 측정):

| 게임 | 생성 끔 | 2배 |
|---|---|---|
| 스위치 젤다 시간의 오카리나 (60fps, 장면 5개) | 57\~60 | 114\~119 |
| PS5 GTA5 성능 모드 (60fps, 2회) | 52\~60 | 103\~111 |
| PS5 GTA5 화질 모드 (30fps, 8회) | 30\~31 | 58\~62 |
| PS5 블러드본 (30fps, 30fps 보정 켬) | 30 | 60 (16.7ms마다 고르게 98%) |

지연(캡처 구간 제외, 생성 버퍼 + 화면 표시): 창 표시 약 37\~39ms, 전체 화면 약 23\~24ms. 휴대폰 240fps로 패스스루와 함께 찍으면, 생성을 끈 미리보기보다 창 약 +36\~50ms, 전체 화면 약 +11\~33ms 늦었습니다(PS5·스위치). 원본 직결(패스스루)보다 늦게 나오므로 대전·리듬 게임은 권장하지 않습니다.


30fps 게임 화질(1.0.1, 실제 프레임과 비교, 높을수록 좋음): GTA5 운전 장면 24.60 → 25.11dB, GTA5 걷기 23.48 → 24.01dB, 젤다(60fps 게임을 33ms 간격으로 시험) 23.29 → 23.72dB. 30fps 게임에서는 생성한 장을 앞→뒤, 뒤→앞으로 두 번 만들어 평균하며(시간이 남을 때만), 고개를 돌리는 캐릭터 주변의 투명한 번짐이 줄어듭니다. 가는 전봇대처럼 갑자기 나타나는 가는 물체는 아직 깨질 수 있습니다.

## 한계

- 아주 빠른 카메라 회전에서 배경이 조금 일렁일 수 있습니다. 30fps 게임은 고개를 갑자기 돌릴 때 얼굴이 일렁일 수 있습니다.
- 3인칭 게임에서 캐릭터가 오래 서 있으면 HUD 보호가 캐릭터를 HUD로 볼 수 있습니다(머리카락 끝 깨짐 → HUD 보호 끄기).
- 반투명 글자 창(도움말·메뉴)이나 게임 속 웹페이지처럼 글자가 많은 화면이 움직일 때 글자가 깨져 보이면 HUD 보호를 끄세요.
- HDR·10비트, 8K는 지원하지 않습니다. 입력은 30\~120fps(29.97·59.94·119.88 포함)를 받습니다.
- 4K(3840×2160) 처리: RTX 5080 엔진 측정에서 RIFE 2배까지 들어갑니다(움직임을 1/4 크기로 자동 계산, 1장 약 14ms). 3배 이상은 모자랍니다. 지연과 여유를 생각하면 FHD를 권장합니다.
- Streaming Center 버전이 바뀌면 다시 확인이 필요할 수 있습니다.

## 구조

| 폴더 | 내용 |
|---|---|
| `engine/` | 프레임 생성 엔진 DLL(C++17): 실시간 스케줄러, RIFE·FSR 3 연결, HUD 보호, 검사 프로그램(`checks/`) |
| `plugin/` | Streaming Center 플러그인(C, GStreamer tracer): 미리보기 연결, 설정 창, 입력 감지, 전체 화면 표시 |
| `installer/` | 설치 프로그램(C#, .NET Framework 4)과 설치 시험 |
| `third_party/` | rife-ncnn-vulkan(ncnn·glslang 포함), FidelityFX SDK 1.1.4 일부, Vulkan-Headers. 우리가 바꾼 곳은 `third_party/PATCHES/` |
| `tools/` | 빌드 도구 받기(`Get-Toolchain.ps1`), 한 번에 빌드(`Build-All.ps1`) |


## 문제 신고

설정 창의 **진단 정보 내보내기** 파일을 [Issues](https://github.com/GamKim/consolesmooth/issues)에 첨부해 주세요.

## 라이선스

MIT ([LICENSE](LICENSE)). 함께 쓰는 부품의 라이선스: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
