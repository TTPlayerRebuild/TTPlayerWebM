# WebM 插件验证记录

本文保留 `2026.10.06` 只读版本的历史结果。当前写入版本、产物哈希及新增验证见 [METADATA_VALIDATION.md](METADATA_VALIDATION.md)；下列旧 DLL 哈希不再对应当前工作目录中的 DLL。

日期：2026-10-06。版本：`2026.10.06`。本记录对应本地构建和实机/虚拟机结果，GitHub Action 尚未远程运行或发布。

## 产物

| 项目 | 结果 |
| --- | --- |
| DLL | `build/Release/ttp_webm.dll`，x86，326,656 字节（319 KiB） |
| DLL SHA-256 | `e8b19fa9d2c3889d6e312b55faa88fdec10414c3a9fdf9c45df22ebb0ba3c47d` |
| ZIP | `build/Release/ttp_webm-2026.10.06.zip`，204,833 字节 |
| ZIP SHA-256 | `1f41092afc635cc3ae5286171febd7e06002ab8aa9a94fa29658e484c7a7a055` |
| 版本 | 文件/产品字符串均为 `2026.10.06`，数值版本 `2026.10.6.0` |
| 导出 | 唯一导出 `ttpGetSoundAddIn`，ordinal 1 |
| 静态依赖 | `kernel32.dll`、`ole32.dll`、`msvcrt.dll`，共 89 个导入 |
| 旧系统 | PE subsystem 5.01，XP / Win7 导出清单审计通过，无额外 VC++ / UCRT 安装要求 |
| 包结构 | 严格为 `AddIn/ttp_webm.dll`、`SHA256SUMS.txt`；包内 DLL 与三个系统实际测试的 DLL 字节一致 |

## 本地接口及解码：46 / 46 通过

私有测试位于 `rebuild/tests/webm_rebuild`，不纳入插件仓库，Action 不构建、不运行、不上传这些测试。

- Opus、Vorbis 各 1 / 2 / 6 / 8 声道，44.1 / 48 kHz，中文路径、`.weba` 和错误 `.bin` 后缀。
- 默认音轨选择；第二条音轨为默认时，实际 PCM、声道数与标题均来自第二条，排除另一音轨标题。
- 带 VP8 视频的 WebM、未知长度 Segment / Cluster、无 Cues、Xiph / EBML 合帧。
- Opus 输出增益、首尾裁剪、完整帧数、PNG 附件封面、标签和多值字段；读取前后源文件哈希不变。
- 0 / 1 / 500 / 1000 ms 定位、结束/越界定位，长 Opus 的 1500 / 3500 / 4500 ms 定位，以及 44.1 kHz 报告时长边界。
- float64 / PCM16 输出，容量限制、非整帧容量、空容量、连续 EOF、信息模式、COM 身份与引用释放。
- 短读拼接、打开时 I/O 错误、解码中 I/O 错误的保持与重新定位恢复、不支持的不可定位流。
- 无音轨、无效/截断/仅头文件、错误 DocType，返回错误而不是崩溃。

参考 PCM 通过 FFmpeg 调用 libopus 和 FFmpeg 原生 Vorbis 解码器生成。注意 FFmpeg 的 libvorbis 包装路径对该批 WebM 的 CodecDelay 又扣除了已经预热的样本，因此未把它作为 Vorbis 长度基准。

采样数量与参考一致；float64 输出与参考 float32 的最大绝对差小于 `2e-5`；PCM16 比较最大差 1 LSB。定位与从头解码后截取相应位置比较，检查阈值 `1e-4`，本批用例最大误差为 0；这不意味着任意 Opus 文件的随机定位均逐位相同。

未专项覆盖固定大小合帧、负 DiscardPadding、加密音轨和 JPEG 图片显示；对应实现不能据此宣称已完成所有组合验证。没有使用真实网络直播作为可播放输入测试。

## 重建版接入

- `extension_mismatch_tests --self` 通过，包括 WebM DocType、WebA 别名、错误/截断 EBML 判断。
- `extension_correction_tests` 22 项通过，包括 `.webm` 建议、`.WEBA` 不误报、其他 EBML 不误识别为 WebM。
- `--source` 使用只放新插件的隔离 AddIn 目录，`.webm` 和错误 `.bin` 得到相同的 Opus PCM：48 kHz、双声道、float64、1,013 ms、777,984 字节。
- `--info` 读取中文 `.weba` 成功；实际原版、重建版命令行打开见下表。

## XP / Win7 解码接口

两台虚拟机均执行 29 个命令检查，结果 `DONE failures=0`：

- 11 个样本分别解码及逐字节比较（22 项），覆盖双/八声道 Opus/Vorbis、长 Opus、两种合帧、未知 Cluster、PNG 封面、默认音轨和带视频文件。
- 短读、信息模式、打开 I/O 失败、不可定位流、读取失败恢复（5 项）。
- Opus / Vorbis 各一次 500 ms 定位（2 项）。

所有比对 PCM 与本机运行同一 DLL 的结果逐字节一致，说明这批样本的跨系统输出一致；这不替代与参考解码器的独立比较。

## 真实播放器验证

| 系统 | 原版 TTPlayer | TTPlayerRebuild | 播放进度及关闭 |
| --- | --- | --- | --- |
| XP SP3，5.1.2600 | Opus WebM、Vorbis WebM、Opus WebA 全部通过 | 同左，全部通过 | 约 2.9～3.6 秒，正常关闭 |
| Win7 SP1，6.1.7601 | 同上，全部通过 | 同上，全部通过 | 约 2.8～3.7 秒，正常关闭 |
| 本机 Windows 11 | 同上，全部通过 | 同上，全部通过 | 约 3.7～3.9 秒，正常关闭 |

共 18 个播放场景通过。测试在隔离目录进行，使用静音的 30 秒样本，确认加载的是指定 `ttp_webm.dll`、出现主窗口、没有错误对话框、保存的播放进度至少 1 秒、无需强制结束。原版正常退出码为 1，重建版为 0；判断没有把原版的常规退出码误判为播放失败。未替换工作区原始 `AddIn` 中的插件。

XP 测试用临时交互计划任务进入已登录桌面，完成后删除该任务。Win7 直接在已登录会话中执行。按先前用户要求，以 Windows 11 代为覆盖现代 Windows 的验证；**没有实际 Windows 10 实机测试**。

## 发行流程

已运行 `build.ps1 -Package -PackageVersion 2026.10.06`，校验 DLL 版本、导入和 ZIP 结构/哈希。PowerShell 脚本语法检查通过，`prepare_release.ps1` 已在私有测试目录生成带第三方声明的发行说明。没有执行 `gh release create`，没有提交或推送代码。

固定依赖下载来源和 SHA-256 在 `third_party/ORIGIN.md`；本地构建复用已下载的对应版本目录。GitHub Action 使用固定地址下载的完整链路仍需远程运行确认，本文不把本地构建等同于 Action 已成功运行。

主要私有证据：`artifacts/matrix.json`、`artifacts/XP-decoder.log`、`artifacts/Win7-decoder.log`、`artifacts/*-host-details.log`、`hosts/interactive-summary.log` 和构建日志。
