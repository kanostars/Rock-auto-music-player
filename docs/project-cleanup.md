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

当前使用 `./scripts/build.ps1` 构建 Release；CLion 的 Debug 目标使用同一份源码与资源。早期自动测试源码与 CTest 目标已移除，不再提供 `-Test` 参数；`tests/fixtures` 仅保留用户 MIDI 和键谱参考资料。

## 2026-09-24 工作台整合后整理

- 移除独立自动演奏页面 `performance_page.cpp/.h`，由 `performance_panel.cpp/.h` 承接输出设置与播放队列；统一曲目库、音符轨道、时间和参数。
- 清除已删除顶部页面导航的样式，更新 README 的操作位置说明。
- 删除 artifacts 中 25 个一次性修改脚本、旧测试日志、第三方查询缓存和独立演奏页的过期截图。当前界面截图及最近验证记录继续保留。
- 保留正式音频、导出包、音高分析资料、测试素材、提取工具、构建依赖及 Release / Debug 程序；临时产物和构建目录仍由 .gitignore 排除。

## 2026-10-03 胶囊小窗整理

- 共用主窗口与小窗的播放控制、曲目列表及图标绘制，移除未使用的旧小窗入口图标、无用引用和定位失败时重复的提示更新。
- 一次性小窗界面验证、演奏定位验证的源码与可执行文件已删除，清除 artifacts 根目录过期测试日志及旧提交说明；保留最近演奏定位验证记录、界面截图和设计资料。
- 正式“九键手碟测试”功能、MIDI 素材、音频资源及第三方依赖继续保留。构建配置不包含临时测试目标，Release / Debug 通过正常构建验证，临时产物不纳入 Git 提交。

## 2026-10-05 小窗细节整理

- 演奏暂停时可切换播放方式；小窗字体、按钮、图标、间距和列表等比例缩放，最小宽度调整为 400 个逻辑像素。
- 增加透明度图标与 20%～100% 调节，保存大小和透明度设置。
- 默认勾选“立即打开目标窗口”，开始演奏时先切换到小窗，再激活目标窗口；初始化失败时恢复主窗口并显示错误。
- 移除小窗布局完成后重复的屏幕边界校正，删除临时验证日志及旧分析工具的构建缓存，更新失效的测试命令与文档说明。音高分析资料及用户参考素材继续保留。
- 临时界面验证源码和可执行文件已移除，必要的功能源码、操作文档纳入提交；构建产物、截图和分析中间文件由 .gitignore 排除。
