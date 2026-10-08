# 应用图标

`app.png` 是已确认使用的简约清新风格图标：浅薄荷绿底色、米白九音手碟、青绿色轮廓和中心播放符号。原图使用内置 ImageGen 生成，圆角外保留透明 alpha。

`app.qrc` 将 PNG 内嵌供 Qt 应用与窗口使用；`app.ico` 包含 16、24、32、48、64、128、256 像素的 32 位图像，供 Windows EXE、任务栏和快捷方式使用。

修改 PNG 后运行 `./scripts/export-icon.ps1` 重新导出 ICO，再重新构建。`app.rc.in` 会从 CMake 的项目版本生成 Windows 文件版本信息。
