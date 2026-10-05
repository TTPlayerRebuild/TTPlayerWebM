# TTPlayer WebM 输入插件

为原版千千静听及重建版增加 `.webm` / `.weba` 音频播放。项目使用原版 Reader ABI，容器解析与音频解码独立于系统 Media Foundation / DirectShow 解码器，也不要求安装 `ttp_ogg.dll`。

这是新格式插件：原版没有提供可供逐字还原的 WebM 插件，复用的是从原版播放器、Ogg 插件伪代码及二进制核实的宿主接口。

## 安装

关闭播放器，将发行 ZIP 中的 `AddIn/ttp_webm.dll` 放入播放器安装目录，随后重新启动。

- 同一 x86 DLL 供原版和重建版使用，支持 XP SP3、Win7 SP1 及现代 Windows；CPU 需要 SSE2。
- 不需要额外安装 VC++ / UCRT 运行库。VC-LTL、YY-Thunks 是构建依赖，随插件静态链接；运行时使用 Windows 自带系统 DLL。
- 带视频的 WebM 只播放所选音轨，不显示视频。
- 支持标签修改、删除和保存，以及 PNG / JPEG 封面的添加、替换、删除；不提供音频编码器。
- 封面使用 Matroska Attachments 兼容扩展，不保证其它 WebM 工具均能显示。原版最后释放 Reader 时保存；重建版通过可选提交接口取得保存结果。

## 支持范围

| 项目 | 实现 |
| --- | --- |
| 容器 | EBML `DocType=webm`，`.webm` 与 `.weba` 别名 |
| 编码 | Opus、Vorbis，1～8 声道；Opus 48 kHz，Vorbis 使用码流采样率 |
| 音轨 | 优先第一个受支持且启用的默认音轨，否则使用第一个受支持的启用音轨 |
| 定位 | 毫秒接口、预解码、精确丢弃至目标采样帧；边界定位钳制到结束 |
| 裁剪与增益 | Opus pre-skip / CodecDelay、DiscardPadding、头部输出增益、Windows 声道顺序 |
| 数据结构 | SimpleBlock、BlockGroup、合帧、无 Cues，以及有限文件内未知长度的 Segment / Cluster |
| 信息 | 时长、平均音频码率、格式、Unicode 路径、Tags、多值标签、音轨标题回退 |
| 标签保存 | 标准 Tags；修改、删除、清空所选音轨的标签，保留未编辑字段与其它音轨信息 |
| 封面 | PNG / JPEG Attachments 读写；经原版 Thumbnail ABI 提供，支持描述和图片类型 |
| 保存保护 | 同目录暂存、重新解析校验、完整备份、失败回滚；音视频包保持原样 |

当前要求**可定位且长度已知的输入流**。不支持持续增长或不可定位的直播流、加密/压缩音轨、Opus 非 0/1 映射族、通用 `.mkv/.mka`、视频显示及音轨切换 UI。读取时建立完整音频包索引，长文件首次打开会有扫描时间；不宣称任意损坏文件均可恢复。

写入要求可识别路径的本地文件、可安全处理的单 Segment 结构及足够的同目录暂存空间。原版接口不能反馈最终 Release 保存错误；重建版可显示提交失败。宿主句柄阻止文件替换时会使用带备份回滚的原流写入，这一路径不具备断电原子性。详见[写入设计与边界](docs/METADATA_WRITE.md)。

## 构建

需要 Visual Studio 2026 的 MSVC x86、Windows SDK、CMake 3.24+、Python 3 和可联网的 PowerShell。源代码包由 CMake 下载并校验固定 SHA-256，不纳入仓库。

```powershell
./build.ps1 -Package -PackageVersion 2026.10.06p1
```

默认版本取北京时间日期；同日补丁形式为 `yyyy.MM.ddpN`。DLL 文件版本与 ZIP 使用同一版本号。构建后自动检查 XP / Win7 静态导入，产物位于：

```text
build/Release/ttp_webm.dll
build/Release/ttp_webm-yyyy.MM.dd.zip
build/Release/SHA256SUMS.txt
```

ZIP 仅含 `AddIn/ttp_webm.dll` 与 DLL 的 `SHA256SUMS.txt`；外部校验文件校验 ZIP。`.github/workflows/build.yml` 独立构建该项目，`Release a Version` 开关控制发布；Action 不构建、运行或上传私有测试。版权和许可证保存在仓库，并由发行说明附上第三方声明，不嵌入 DLL。

## 文档

- [实现与原版接口依据](docs/IMPLEMENTATION.md)
- [标签、封面写入与保存保护](docs/METADATA_WRITE.md)
- [写入版本验证记录](docs/METADATA_VALIDATION.md)
- [验证记录与范围](docs/VALIDATION.md)
- [依赖来源、固定版本和许可证](third_party/ORIGIN.md)
