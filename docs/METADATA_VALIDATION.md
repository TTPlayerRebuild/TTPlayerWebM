# WebM 标签、封面写入验证

日期：2026-10-06。版本：`2026.10.06p1`。本地构建及真实宿主/虚拟机测试完成；未提交、推送或远程运行 GitHub Action。

## 产物

| 项目 | 结果 |
| --- | --- |
| DLL | `build/Release/ttp_webm.dll`，x86，378,880 字节（370 KiB） |
| DLL SHA-256 | `f23e1b270fa399c1dff0f84dce6c91922cc94a2b728b3319310603b1b130686b` |
| ZIP | `build/Release/ttp_webm-2026.10.06p1.zip`，240,540 字节 |
| ZIP SHA-256 | `9dc05902efbb8ffe21f89333762de0873851eff27fef5d91dd5fbdb30a96fabf` |
| 版本 | 文件/产品版本 `2026.10.06p1`，数值 `2026.10.6.1` |
| 导出 | 唯一 `ttpGetSoundAddIn`，ordinal 1 |
| 导入 | kernel32.dll、ole32.dll、shlwapi.dll、msvcrt.dll；101 项，通过 XP / Win7 清单检查 |
| 子系统 | 5.01；无额外 VC++ / UCRT 安装要求 |
| 包内容 | 仅 `AddIn/ttp_webm.dll`、`SHA256SUMS.txt`；外部 SHA256SUMS 校验 ZIP |

打包 DLL 与从 XP、Win7 回传的实际测试 DLL 逐字节一致。保留旧版 `2026.10.06` ZIP，未覆盖播放器原始 AddIn 目录。相比只读 DLL 增加 52,224 字节。

## 本地：46 项解码回归、30 项写入测试全部通过

46 项解码测试覆盖范围沿用[初版验证记录](VALIDATION.md)，在最终 DLL 上重新执行。

30 项写入测试包括：

- Opus、Vorbis、多音轨、带视频、无 Cues、有限未知长度 Segment / Cluster、EBML lacing 文件的标签写入。
- Unicode 字段值、标准及自定义字段、字段删除、清空标签、删除标题抑制回退。
- 原版最终 Release 保存、显式 Commit、关闭只读源后替换文件、无修改不写回。
- PNG / JPEG 添加、替换、删除；描述及图片类型的部分更新。
- 不相关音轨的有效标签保持一致，文本附件保留，已删除封面对应的附件目标标签清理。
- CRC、SeekHead 实际偏移、Attachments 数量独立检查；连续五次相同保存文件大小均为 15,126 字节。
- 短写、读取失败、Commit 失败、只读文件、URL、文件被外部修改、非法封面/字段/索引。
- Commit 结果可重复查询；提交后的修改/解码拒绝；Release 不重试已失败提交。
- 保存和回滚均失败时，保留完整原文件备份与可重新解析的新文件；注入后的损坏目标不冒充保存成功。

对正常保存测试独立比较实际音视频压缩包的 SHA-256、时间戳、时长、大小，并比较所选音轨解码 PCM。封面新增产生的缩略图流不混入音视频包比较。结果均保持原媒体数据与 PCM 不变。

## XP / Win7：每系统 24 场景、47 项命令检查

| 系统 | 写入/故障命令检查 | 回传文件独立复核 |
| --- | --- | --- |
| XP SP3 5.1.2600 | 47 / 47 | 24 / 24 |
| Win7 SP1 6.1.7601 | 47 / 47 | 24 / 24 |

场景覆盖两种音频编码、带视频、多音轨、未知长度 Cluster、原版释放保存、显式提交、替换路径、清空/删除、PNG/JPEG 新增/替换/删除/描述，以及 Tags / Attachments / Segment CRC。错误场景覆盖短写回滚、Commit 失败回滚、读取失败、只读、URL 和外部文件修改。

将客机实际输出文件复制回本机，再独立检查音视频压缩包、PCM、CRC、SeekHead 和标签/封面结果；错误场景核对原文件逐字节不变（外部修改案例保留外部追加的字节）。回滚也失败和非法参数的专项故障注入仅在本机运行，不计入客机 24 场景。

首次 XP 测试发现 PNG 新增和类型/描述修改返回 E_FAIL，Win7 正常。定位为 CRC 表局部静态初始化路径，改为编译期常量后，两个场景及新增容器 CRC 保存均在 XP 重测通过。这说明静态导入合格不等于所有运行路径已兼容。

## 真实播放器：三个系统共 24 / 24

每个系统分别用原版 TTPlayer、TTPlayerRebuild，执行 Opus 和 Vorbis 的“打开播放 → 文件属性修改 Title → 保存 → 关闭 → 重新打开播放”。每个宿主包含两次保存、两次重新播放，共 8 项/系统。

| 系统 | 原版 | 重建版 |
| --- | --- | --- |
| XP SP3 | 4 / 4 | 4 / 4 |
| Win7 SP1 | 4 / 4 | 4 / 4 |
| 本机 Windows 11 | 4 / 4 | 4 / 4 |

检查指定插件确实加载、文件属性保存按钮启用、标题实际写入文件、无错误模态窗口、无需强制终止、重新播放进度超过 1 秒。原版正常退出码为 1，重建版为 0。重建版保存会停止当次播放，因此保存案例不要求 PlayingTime 非零；重新打开案例验证正常播放进度。

XP 使用临时交互式计划任务，完成后已删除。按照用户要求，Windows 11 代为验证现代 Windows；没有实际 Windows 10 机器，不能写成 Win10 实测通过。

封面读写通过原版 Thumbnail ABI 的独立测试验证；本次没有逐个点击两套宿主的图片导入/删除按钮，也没有宣称其它 WebM 应用或浏览器的封面兼容性已通过。

## 私有测试与发布边界

全部测试源代码和样本只在 `rebuild/tests/webm_rebuild`，不纳入本插件仓库，不构建到发行 DLL，Action 不运行或上传测试。主要证据：

- `final-read-matrix.log`、`artifacts/matrix.json`
- `write-matrix.log`、`write-artifacts/results.json`
- `write-artifacts/XP-write.log`、`Win7-write.log`、`vm-independent.json`
- `write-artifacts/XP-hosts/interactive-*.log`、`Win7-hosts/interactive-*.log`
- `hosts/interactive-*.log`、`write-package.log`

许可证保留在仓库，发行说明链接并附上第三方声明，不嵌入 DLL。Action 发布说明已更新为可写功能；未执行网络发布。保存失败、临时空间和接口限制详见[写入设计](METADATA_WRITE.md)。
