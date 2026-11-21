# ConsoleSmooth 5 分钟上手（GTA6 · Switch 2）

[한국어](QUICK-START.ko.md) · [English](QUICK-START.en.md) · [日本語](QUICK-START.ja.md) · [完整说明（英文）](GUIDE.md)

如果你已经在用圆刚（AVerMedia）采集卡和 **AVerMedia Streaming Center**，不需要再装别的软件。只装一个插件，就能把 PS5 GTA6 的 30fps 变成 60fps，把 Switch 2 的 60fps 变成 120fps。

插件界面只有韩文和英文。下面的按钮名称用英文界面的写法。

## 先确认

| 项目 | 要求 |
|---|---|
| 采集卡 | 圆刚 AVerMedia。已测试：GC573、GC313 Pro、GC553 Pro。GC553G2 还没有实测（用的是同一个 Streaming Center，大概率可用）。 |
| 软件 | AVerMedia Streaming Center 1.8.22.2 或 1.9.19.1。在 OBS、RECentral 里不能用。 |
| 显卡 | 至少 AMD RX 6600 · GeForce RTX 2060 级（FSR 3）/ RTX 3070、4060 级（RIFE）。NVIDIA Video Frame Generation 需要 RTX 40/50。 |
| 显示器 | 120Hz 及以上 |

## 安装和开启

1. 在 [Releases](https://github.com/GamKim/consolesmooth/releases/latest) 下载 `ConsoleSmooth-Plugin-Setup-<版本>.exe`。安装程序没有签名，如果 Windows 提示，点 **更多信息 → 仍要运行**。请只从这个 GitHub 页面下载。
2. **完全关闭 Streaming Center**（包括托盘图标），再点 **Install**。
3. 打开 Streaming Center，画面进来后点工具栏的 **Frame generation settings** → **Recommended** → **Apply**。它会在你的电脑上实际试跑约 25 秒，然后选好设置。
4. 点 **Turn frame generation on** 或按 **Ctrl+Alt+F** 开启，再按一次关闭。

## 推荐用法

| 游戏 | 倍数 | 说明 |
|---|---|---|
| PS5 GTA6（30fps） | **2 倍** → 60 | 3 倍、4 倍（→ 90、120）也能用，但延迟和画面破损会增加。建议先用 2 倍。 |
| Switch 2（60fps） | **2 倍** → 120 | 适合 120Hz 显示器。 |
| 格斗、音游、射击、竞技游戏 | 不建议使用 | 画面比原画面晚显示，不适合讲究时机的游戏。 |

- 想要最低延迟：在 **Frame generation** 标签的 Display 选 **Full screen**。低延迟视图会自动一起打开，点其他窗口时全屏也不会关闭。开发者实测比原画面晚约 9~19ms（RTX 5080）。
- 想把生成的帧录进视频或直播：在 **Display · recording** 标签把录制来源设为 **With generated**。
- 设置窗口最下面的状态栏会显示当前生成 fps，以及原画面从到达到显示所用的时间（程序内部测量）。
