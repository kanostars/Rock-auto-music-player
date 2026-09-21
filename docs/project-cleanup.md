# 音频替换与项目整理

日期：2026-09-20。

## 当前音频

九键共 36 个原始 SIFT 采样统一存放在 `assets/handpan`，来源和每文件 SHA-256 记录在同目录的 `manifest.json`。Qt 资源文件只引用这里的音频。播放器使用 44.1 kHz 立体声，每键四个采样轮换，并保持暂停、继续与定位的一致性。

## 已删除

本次清理删除 132 个冗余文件，共约 76.42 MiB；此数值为删除文件的大小总和，不是计入新采样和重新构建后的项目净缩减量。

- 根目录 `main.exe`：早期遗留的小型可执行文件；当前入口为 `build/RockAutoMusicPlay.exe` 和 CLion Debug 目标。
- 旧的九个视频切片 WAV：已被 36 个程序原始采样替代。
- `scripts/prepare_handpan.py` 和 `video_pitch_analysis/`：旧录音处理脚本、提取音频、截图及映射中间文件，已不参与构建或测试。
- `.deps/qtbase.7z`、`.deps/midifile.tar.gz`：已经解压并安装的下载包；Qt SDK 和第三方源码仍在。
- `artifacts/sift-handpan-extracted/`：重复 WAV、九音副本、提取临时文件及 .NET 编译缓存。正式 WAV 已逐文件核对 SHA-256 后移入 `assets/handpan`；提取源码移至 `tools/audio-extraction`。
- `artifacts` 根目录旧截图、旧音频预览及过期测试文本：新测试输出已放入分类目录。

具体删除路径、文件数量和字节数见 [删除记录](project-cleanup-files.json)。只操作了本项目目录，没有修改用户提供的原程序或原视频。

## 保留与归类

- `src`、`tests`、`samples`、`docs`、`third_party`：功能源码、测试、演示/回归 MIDI、设计和第三方许可继续保留。
- `.deps/Qt`、`.idea`、`build`、`cmake-build-debug`：构建依赖、IDE 配置、Release 与 Debug 产物继续保留。
- `artifacts/screenshots`：自动测试截图。
- `artifacts/audio/pirates-handpan-preview.wav`：使用新采样渲染的《加勒比海盗》20 秒试听。
- `artifacts/exports/SIFT-handpan-9-notes-and-36-samples.zip`：上一步交付的完整原始音频包，作为导出成品保留；应用不读取此压缩包。
- `tools/audio-extraction`：提取与导入工具及用法，不是应用的构建依赖。

## 验证方式

`./scripts/build.ps1 -Test` 构建 Release 并运行核心转换、音频、程序启动和界面测试。音频测试核对 36 个资源哈希和帧数，并覆盖双声道、轮换、同键合并、暂停恢复和完整采样长度。CLion 的 Debug 目标也使用同一份源码与资源。
