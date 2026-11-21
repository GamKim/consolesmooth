# ConsoleSmooth 5分スタート（GTA6 · Switch 2）

[한국어](QUICK-START.ko.md) · [English](QUICK-START.en.md) · [简体中文](QUICK-START.zh.md) · [詳しい使い方（英語）](GUIDE.md)

AVerMedia のキャプチャーボードと **AVerMedia Streaming Center** をすでに使っているなら、ほかのソフトは要りません。プラグインを1つ入れるだけで、PS5 の GTA6 を 30fps から 60fps に、Switch 2 を 60fps から 120fps にして見られます。

プラグインの画面は韓国語と英語だけです。下のボタン名は英語表示のものです。

## まず確認

| 項目 | 条件 |
|---|---|
| キャプチャーボード | AVerMedia。確認済み: GC573、GC313 Pro、GC553 Pro。GC553G2 はまだ確認していません（同じ Streaming Center を使うので動く可能性が高いです）。 |
| ソフト | AVerMedia Streaming Center 1.8.22.2 または 1.9.19.1。OBS・RECentral では動きません。 |
| GPU | 最低 AMD RX 6600 · GeForce RTX 2060 クラス（FSR 3）/ RTX 3070・4060 クラス（RIFE）。NVIDIA Video Frame Generation は RTX 40/50。 |
| モニター | 120Hz 以上 |

## インストールしてオンにする

1. [Releases](https://github.com/GamKim/consolesmooth/releases/latest) から `ConsoleSmooth-Plugin-Setup-<バージョン>.exe` をダウンロードします。署名がないので Windows の警告が出たら **詳細情報 → 実行** を押してください。
2. Streaming Center を**完全に終了**（タスクトレイのアイコンも）してから **Install** を押します。
3. Streaming Center を起動し、ゲーム画面が映ったらツールバーの **Frame generation settings** → **Recommended** → **Apply** を押します。約25秒この PC で実際に試してから設定を選びます。
4. **Turn frame generation on** ボタンか **Ctrl+Alt+F** でオン。もう一度押すとオフです。

## おすすめ

| ゲーム | 倍率 | メモ |
|---|---|---|
| PS5 GTA6（30fps） | **2倍** → 60 | 3倍・4倍（→ 90・120）も使えますが、遅延と画面の崩れが増えます。まずは2倍がおすすめです。 |
| Switch 2（60fps） | **2倍** → 120 | 120Hz モニター向けです。 |
| 格闘・音ゲー・FPS・対戦ゲーム | 使わない | 元の映像より遅れて表示されるので、タイミングが大事なゲームには向きません。 |

- 遅延をいちばん短くするには **Frame generation** タブの Display で **Full screen** を選んでください。低遅延表示も自動でオンになり、ほかのウィンドウをクリックしても全画面は閉じません。開発者の測定で元の映像より約 9〜19ms 遅れでした（RTX 5080）。
- 生成したフレームを録画・配信に入れるには **Display · recording** タブで録画ソースを **With generated** にします。
- 設定ウィンドウ一番下の状態表示に、いま生成している fps と、元の映像が届いてから表示されるまでの時間（プログラム内で測った値）が出ます。
