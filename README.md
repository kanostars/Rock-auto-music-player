# ROCK / 洛克手碟演奏器

**v1.2.6** · Windows 九键音乐工作台，将 MIDI 转换为固定九音键谱，支持编辑、手碟试听、跟练与自动演奏。

By 北辰_kano · [GitHub](https://github.com/kanostars/Rock-auto-music-player) · [bilibili](https://space.bilibili.com/481122212)

**此软件永久免费，一切商用皆是骗子。**

## 快速开始

1. 在 [Releases](https://github.com/kanostars/Rock-auto-music-player/releases) 下载安装版，或完整解压便携 ZIP 后运行 `RockAutoMusicPlay.exe`。
2. 导入 / 拖入 MIDI，可批量追加；也可使用 `samples/studio-demo.mid` 体验。选择音轨、调整参数后点击“应用设置 · 重新转换”。
3. 使用“手碟试听”或“跟练练习”；自动向游戏按键需另行安装 [Interception](https://github.com/oblitum/Interception) 驱动并重启，再选择输出键盘与目标窗口。

首次启动会显示功能帮助。之后可点击设置右侧的 ⓘ，按步骤高亮查看功能，胶囊小窗会实际打开介绍。

## 主要功能

- **转换与编辑**：自动移调、八度折叠、近似音 / 跳过处理；支持框选、移动、拉伸、升降音级和撤销 / 重做。`Ctrl+C / X / V` 复制、剪切并在游标处粘贴音符，保留相对节奏。
- **多轨与区间**：各音轨以不同填充色及名称前的色标区分。同音名无边框，近似音黄框，已编辑蓝框，冲突红框；跳过音符为无框灰块。绿色边界选择播放区间，支持整段删除、插入空白与全屏轨道。
- **试听与演奏**：曲目列表、播放顺序、倍速、进度和音量控制；自动演奏支持倒计时、失焦暂停及按键释放。点击 Logo 或按 `F12` 切换置顶播放胶囊，可切歌并调整大小与透明度。
- **歌曲跟练**：时间轴 / 简谱、自动播放 / 等待按键跟弹、A/B 循环、节拍器和自定义练习键位；简谱可导出 PNG / ZIP。跟练胶囊提供当前按键与单小节简谱，在游戏前台也可使用。
- **工程与导出**：`.rockproj` 保存曲目、编辑记录、参数、进度及布局，支持内嵌数据或引用 MIDI，启动时恢复最近工程。左键“导出 MIDI”保存整曲，右键仅导出绿色区间；支持复制键谱 / 手弹谱及导出 TXT。编辑不改写原始 MIDI，请保存工程或导出结果。
- **外观与设置**：浅色 / 深色 / 跟随系统、11 组配色、自定义强调色与参考图取色；壁纸库支持静态图片、GIF、静音循环视频及重命名。面板适度透明，音符网格、时间刻度和音高栏保持不透明。设置中还可管理快捷键、驱动、手碟测试与运行日志。

修改转换参数后需先应用设置；同键过密冲突需调整后才能试听或演奏。简谱以 1/16 网格排版，复杂节奏请结合试听校对。

## 九键与常用快捷键

| 按键 | B | F | G | H | J | K | T | Y | U |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 音高 | A2 | E3 | F3 | G3 | A3 | B3 | C4 | D4 | E4 |

| 默认快捷键 | 操作 |
| --- | --- |
| `Ctrl+O` / `Ctrl+Shift+O` | 导入 MIDI / 工程 |
| `Ctrl+S` / `Ctrl+Shift+S` | 保存工程 / 另存为 |
| `Ctrl+P` | 导出当前 MIDI |
| `Space` | 试听 / 跟练播放与暂停 |
| `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | 复制 / 剪切 / 粘贴所选音符 |
| `Ctrl+Z` / `Ctrl+Y` | 撤销 / 重做 |
| `Ctrl+W` / `Ctrl+R` | 全局演奏暂停与继续 / 终止 |
| `Ctrl+Q` / `Ctrl+E` | 全局上一首 / 下一首 |
| `F12` / `F11` | 胶囊小窗 / 全屏轨道 |
| `Esc` | 退出引导、关闭弹窗或返回工作台 |

多数快捷键可在设置修改；音符编辑快捷键在轨道区生效，演奏暂停 / 终止组合仅在演奏会话中注册。

## 构建与发布

需要 C++20、CMake ≥ 3.24、Qt ≥ 6.5（Widgets / Concurrent），编译器与 Qt 工具链须匹配。

```powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH="<Qt SDK 路径>" -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

使用 CLion 自带 MinGW / CMake，Qt 位于 `.deps/Qt` 时可直接运行 `./scripts/build.ps1`。Windows 构建会部署运行依赖，手碟采样与图标已内嵌。

```powershell
./scripts/package-release.ps1            # 构建并打包 ZIP
./scripts/package-release.ps1 -SkipBuild # 使用已有构建
./scripts/package-installer.ps1 -SkipBuild -IsccPath "<ISCC.exe 路径>"
```

发布包位于 `artifacts/releases/`，须保留 EXE 同目录 DLL 与插件。安装版需要 Inno Setup 6.5+ 及 [官方简体中文语言文件](https://github.com/jrsoftware/issrc/blob/main/Files/Languages/ChineseSimplified.isl)，可用 `-LanguageFile` 指定。Qt SDK 与构建产物不提交到源码。

## 范围与许可

支持 SMF Format 0/1、PPQ、原曲变速、固定 BPM 与中文路径；不支持 Format 2、SMPTE、实时 MIDI、弯音 / 踏板效果或音频导出。自动演奏仅支持 Windows，游戏兼容性取决于目标环境；程序不安装 Interception 驱动。

项目采用 [MIT 许可](LICENSE)。第三方来源与许可见 [依赖说明](third_party/SOURCES.md)、[Interception](third_party/interception/README.md)、[运行库](third_party/runtime-licenses/README.md) 和 [手碟采样](assets/handpan/README.md)。
