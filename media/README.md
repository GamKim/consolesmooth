# 비교 영상과 설정 창 사진 / Comparison videos and settings photos

ConsoleSmooth 1.0.1 소개 글에 쓴 영상과 사진입니다. 측정 환경: RTX 5080, AVerMedia GC573, 160Hz 모니터, Streaming Center.
Videos and photos used in the ConsoleSmooth 1.0.1 posts. Test setup: RTX 5080, AVerMedia GC573, 160 Hz monitor, Streaming Center.

`-en`이 붙은 파일은 영어 이름표 버전입니다. / Files ending in `-en` have English labels.

## 화질 비교 (왼쪽 원본, 오른쪽 생성 켬, 반속) / Quality (left original, right generated, half speed)

Streaming Center 녹화(생성 프레임 포함)를 나란히 붙이고, 사이트 재생이 60fps까지라 반속으로 만들었습니다.
Side-by-side Streaming Center recordings (generated frames included), slowed to half speed because most sites play up to 60 fps.

| 파일 / File | 게임 / Game | 설정 / Setting |
| --- | --- | --- |
| `videos/젤다-하이랄평원-끔vs2배-반속.mp4` (+`-en`) | 스위치 젤다 시간의 오카리나, 하이랄 평원 (60fps) / Switch Zelda Ocarina of Time, Hyrule Field | RIFE 2배 / 2x |
| `videos/젤다-성안-끔vs2배-반속.mp4` | 스위치 젤다, 성 안뜰 / castle courtyard | RIFE 2배 / 2x |
| `videos/젤다-빠른장면-끔vs2배-반속.mp4` | 스위치 젤다, 빠른 장면 / fast scene | RIFE 2배 / 2x |
| `videos/GTA5화질-빠른회전-끔vs2배-반속.mp4` (+`-en`) | PS5 GTA5 화질 모드 (30fps), 빠른 회전 / quality mode, fast turn | RIFE 2배 / 2x |
| `videos/GTA5화질-운전-끔vs2배보정-반속.mp4` | PS5 GTA5 화질 모드 (30fps), 운전 / driving | RIFE 2배 + 30fps 보정 / 2x + 30 fps pacing |
| `videos/GTA5화질-운전-끔vs3배-반속.mp4` | PS5 GTA5 화질 모드 (30fps), 운전 / driving | RIFE 3배 / 3x |
| `videos/GTA5성능-끔vs2배-반속.mp4` (+`-en`) | PS5 GTA5 성능 모드 (60fps) / performance mode | RIFE 2배 / 2x |
| `videos/블러드본-끔vs2배보정-반속.mp4` (+`-en`) | PS5 블러드본 (30fps) / Bloodborne | RIFE 2배 + 30fps 보정 / 2x + 30 fps pacing |

30fps 보정은 1.0.1에서 기본 꺼짐입니다. / 30 fps pacing is off by default in 1.0.1.

## 지연 측정 (8배 느리게) / Latency (8x slow motion)

휴대폰 240fps로 패스스루 화면과 함께 찍고 8배 느리게 만든 영상입니다. 숫자는 저장소 README에 있습니다.
Filmed with a phone at 240 fps next to the passthrough picture, slowed 8x. The numbers are in the main README.

| 파일 / File | 내용 / What |
| --- | --- |
| `videos/지연-PS5-A3-전체화면.mp4` (+`-en`) | PS5, ConsoleSmooth 2배 전체 화면 vs 패스스루 / 2x full screen vs passthrough |
| `videos/지연-스위치-A3-전체화면.mp4` (+`-en`) | 스위치, ConsoleSmooth 2배 전체 화면 vs 패스스루 / Switch, 2x full screen vs passthrough |
| `videos/지연-PS5-A5-오리.mp4` | PS5, Lossless Scaling vs 패스스루 / Lossless Scaling vs passthrough |
| `videos/지연-스위치-A5-오리.mp4` | 스위치, Lossless Scaling vs 패스스루 / Switch, Lossless Scaling vs passthrough |

Lossless Scaling 지연 영상은 Streaming Center 창에 Lossless Scaling을 적용해 찍었습니다. 나중에 확인해 보니 이 방식에서는 Lossless Scaling이 화면을 120fps로 읽어 생성을 거의 하지 않았습니다. 제대로 생성된 상태의 값이 아닐 수 있습니다.
The Lossless Scaling latency clips were filmed with Lossless Scaling applied to the Streaming Center window. We later found that in this setup Lossless Scaling reads the picture as 120 fps and barely generates frames, so these may not be values from proper generation.

## Lossless Scaling(LLCV) vs ConsoleSmooth

| 파일 / File | 내용 / What |
| --- | --- |
| `videos/PS5 GTA5 화질모드 운전 - 오리(LLCV) vs ConsoleSmooth (목표 120).mp4` | PS5 GTA5 화질 모드 운전. Lossless Scaling(LLCV 화면 위, 목표 120) vs ConsoleSmooth 2배. PC 화면 녹화 60fps / PS5 GTA5 quality mode driving, Lossless Scaling on LLCV (target 120) vs ConsoleSmooth 2x, PC screen recording at 60 fps |
| `videos/스위치 젤다 하이랄평원 - 오리(LLCV) vs ConsoleSmooth (목표 120).mp4` | 스위치 젤다 하이랄 평원, 같은 방식 / Switch Zelda Hyrule Field, same method |

Lossless Scaling 설정 / settings: 3.2.2, LSFG 3.1, 적응형 목표 120 (adaptive target 120), 흐름 배율 최대 (flow scale max), 성능 우선 끔 (performance off), WGC, 대기열 0 (queue 0), 스케일링 끔 (scaling off), 수직 동기화 끔 (V-Sync off), 최대 프레임 대기 10 (max frame latency 10), HDR·G-Sync 끔 (off).

결과는 위 PC와 설정에서 나온 것이며 다른 환경에서는 다를 수 있습니다. / Results are from the PC and settings above and may differ elsewhere.

## 설정 창 사진 / Settings window (1.0.1)

| 파일 / File | 탭 / Tab |
| --- | --- |
| `images/설정창-프레임생성-1.0.1.png` | 프레임 생성 / Frame generation |
| `images/설정창-1.0.1-표시녹화.png` | 표시 · 녹화 / Display · Recording |
| `images/설정창-1.0.1-고급.png` | 고급 / Advanced |
