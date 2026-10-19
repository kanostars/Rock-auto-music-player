# 应用图标

`app.png` 使用用户提供的《阳光下的精灵手碟乐章》，完整保留原图画面，作为应用与安装包图标。

`app.qrc` 将 PNG 内嵌供 Qt 应用与窗口使用；`app.ico` 包含 16、24、32、48、64、128、256 像素的 32 位图像，供 Windows EXE、任务栏和快捷方式使用。

修改 PNG 后运行 `./scripts/export-icon.ps1` 重新导出 ICO，再重新构建。`app.rc.in` 会从 CMake 的项目版本生成 Windows 文件版本信息。
