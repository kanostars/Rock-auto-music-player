# 离线提取手盘资源

此工具用于复现用户提供的 SiftHandpanWorkbench v0.1.4 音频提取。正常构建桌面软件无需运行它，也无需安装 .NET 或 Python。

提取工具使用 .NET 6 SDK，静态读取单文件包和程序集资源，不执行源程序；导入工具只依赖 Python 标准库。

在项目根目录执行，替换源程序路径：

```powershell
dotnet run --project tools/audio-extraction/Extract.csproj -- "D:/path/to/SiftHandpanWorkbench.exe" "artifacts/audio-extraction"
python tools/audio-extraction/import_samples.py artifacts/audio-extraction
./scripts/build.ps1 -Test
```

导入器要求九键各 4 个完整采样，在校验所有文件的 SHA-256 和 WAV 格式后，将原始字节写入 `assets/handpan`，并生成来源清单及 `assets/soundbank.qrc`。工具只用于当前包结构，不能作为任意程序的通用提取器。

提取过程的中间文件写入指定的 artifacts 目录；源程序必须在导入时仍可读，以记录其哈希。`bin/`、`obj/` 是可重新生成的 .NET 构建缓存。
