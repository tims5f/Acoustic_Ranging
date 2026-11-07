# 演示媒体

两段 GIF 均来自项目成员提供的实板录像，已顺时针旋转至便于阅读的方向，循环播放且无音轨。

| 文件 | 原视频片段 | 输出 |
| --- | --- | --- |
| `demo.gif` | 00:38～00:43 | 双板运行，640 × 362，8 fps |
| `waveform.gif` | 02:43～02:51 | 全屏波形及操作，640 × 362，8 fps |

原始视频不随仓库分发。重新生成需安装 FFmpeg，或运行 `python -m pip install imageio-ffmpeg`，在仓库根目录执行：

```powershell
python tools/make_demo_gif.py input.mp4 --start 38 --duration 5 --width 640 --fps 8 --rotate cw --output docs/assets/demo.gif
python tools/make_demo_gif.py input.mp4 --start 163 --duration 8 --width 640 --fps 8 --rotate cw --output docs/assets/waveform.gif
```

这些视频片段用于展示实物运行，不能独立证明测距精度、同步误差或全部验收指标。
