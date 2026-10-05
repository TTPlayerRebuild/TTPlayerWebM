# WebM 插件实现与原版接口依据

## 1. 原版与重建版的边界

原版 `TTPlayer.exe` 伪代码中的 `004CABC0` / `004C88D9` 负责插件发现，`004CBC1F` 选择 Reader，`004CD045` 连接压缩格式解码路径；`004CBA11` 的文件头格式提示没有 WebM。系统回退为 `004E8BD6` 的 DirectShow，`004E8912` 提供部分后缀的预加载判断。原版是否能经系统播放 WebM，取决于机器上的 DirectShow 过滤器。

原版 Ogg 插件 `6055268D → 60557E57` 走 Vorbisfile / Ogg 输入。Opus 或 Vorbis 解码器本身不能拆开 WebM 的 EBML 数据结构。因此扩展 `.webm` 文件名列表或把 WebM 交给 `ttp_ogg`，都不能替代 WebM 解复用。

重建版还有 Media Foundation 回退，本机此前的系统路径可以播放样本，但这不是 XP / Win7 通用支持。新插件把容器、解码及宿主适配放在同一个 DLL 中。

## 2. ABI 与所有权

- `ttpGetSoundAddIn`：唯一导出，ordinal 1，x86 stdcall。
- SoundAddIn 枚举一个 Reader 工厂，声明 `*.webm;*.weba`，不枚举 Writer。
- Reader 保留原版 16 个方法；Metadata、Thumbnail 保留原版接口布局及 GUID。
- `IUnknown` 身份统一返回 Reader；引用计数使用 Interlocked，字符串和格式由 `CoTaskMemAlloc` 返回。
- Reader 的 Open flags 0 为只读信息；bit 0 开启解码，bit 1 选择 float64，否则输出 PCM16。输出遵循宿主容量及整采样帧对齐。
- 支持原版扩展流的 ReadWait 槽 14、回调槽 4；否则退回标准 IStream。回调、I/O 错误使用 HRESULT 返回，不让 C++ 异常穿过宿主 ABI。
- 定位始终可用；符合本地安全写入条件时开放标签与封面编辑能力。保留原版最终 Release 保存，并增加重建版已有的可选 MetadataCommit 接口返回实际提交结果。

## 3. 容器与选轨

`Source` 把有限、可定位的 IStream 适配为 libwebm IMkvReader，以 64 KiB 缓存减少逐字节 EBML 解析引发的宿主调用。支持短读拼接；失败的部分缓存会立即作废，重新定位后能够恢复读取。

libwebm 负责 Segment / Track / Cluster / Block 和三种合帧拆分。补充的有界 EBML 读取用于默认/启用标记、Tags、Attachments，以及未知长度 Cluster 后的元数据。DocType 必须为 `webm`；通用 Matroska 不在本版声明范围。

跳过视频与未知编码，优先第一个启用且默认的 Opus / Vorbis 音轨；没有默认匹配时选第一个可用音轨。读出完整音频包偏移、包长、采样数与裁剪信息后释放 libwebm Segment 对象，只保留音频索引。播放时按偏移读取选中音轨，不解码视频。

限制包括最多 256 条音轨、100 万 Cluster、400 万音频包、单包/CodecPrivate 16 MiB、单封面 16 MiB、64 张封面、4096 个标签和 8 层嵌套标签。超界返回错误；这些是实现边界，不代表对任意大文件均有固定内存开销。尤其建索引阶段仍需扫描全文件结构，长文件打开耗时和内存会增加。

## 4. 解码、时间与定位

### Opus

直接使用 libopus multistream 解码，处理 OpusHead 中的 pre-skip、Q8.8 输出增益、0/1 映射族及声道映射。容器 CodecDelay 转为 48 kHz 采样帧，与 pre-skip 检查一致性，容许纳秒换算造成的一帧差异。DiscardPadding 正值丢弃块末尾，负值丢弃开头，合帧时跨各包分配。

### Vorbis

CodecPrivate 的 Xiph lacing 拆成 identification / comment / setup 三个头，通过 libvorbis 校验。依据前后包的 block size 计算输出帧数。Vorbis 合成本身已处理起始半块预热，**不会再把 CodecDelay 从已解出的 PCM 中重复扣除**；否则会丢失真实样本并造成时间轴错误。

### 公共处理

- 将 Vorbis 声道顺序转换为 Windows 顺序；Opus family 1 使用同一顺序规则。
- 时长根据选中音轨解码及裁剪后的帧数计算，不直接采用可能包含较长视频的 Segment Duration。
- 时间戳容许两个 TimecodeScale tick 的舍入误差；大于容差的正间隙补静音，无法解释的反向重叠返回错误。
- 定位先二分包索引，再向前预热，解码后逐帧丢弃至请求位置；毫秒接口取整，定位到所报告时长即 EOF。
- Opus 预热至少一秒，并遵从更大的 SeekPreRoll；比规范常用的 80 ms 更长，用于减少持续音调上的预测状态差异。**随机定位的有损解码结果不保证与从头解码逐位相同**，已用误差阈值校验。
- 已输出的有效 PCM 先交给宿主，后续包/I/O 错误在下一次读取报告，并保持至成功定位。无数据的正常结束返回 `S_FALSE`，不与错误混淆。

## 5. 标签和封面

读取全局及选中 TrackUID 的 Tags，排除其他音轨/章节/附件的标签。保留重复字段，Metadata Get 使用 `; ` 拼接；对 Year、Track、AlbumArtist 做旧接口名称映射。Title 缺失时使用所选音轨 Name，再退回 Segment Title。

PNG/JPEG Attachments 经基本签名验证后提供给 Thumbnail。封面字节保持原样，稳定保存描述、MIME 和 Picture 结构；含 `back` 的文件名识别为背面，自有 `ttplayer-cover-<type>-<UID>` 名称恢复其它类型，其余默认为正面，正面优先。这是兼容附件型封面的支持，不表示所有 WebM 工具都会写入或承认此扩展。

`2026.10.06p1` 增加 Tags 与 Attachments 的修改、删除和保存。通过保留媒体相对偏移、重建 SeekHead、更新 CRC、暂存校验与备份回滚实现，不重新编码音视频；其它音轨与无关附件保持原有含义。详见[写入设计与边界](METADATA_WRITE.md)及[新增验证](METADATA_VALIDATION.md)。

## 6. 重建版接入

重建版 `audio/format_probe` 新增有界 EBML DocType 检查：常规格式仍只读初始 16 字节，遇到 EBML 才扩展到 4096 字节；流探测恢复原位置。只有 `DocType=webm` 才提示 `.webm`，合法 `.weba` 保留，不误报一般 Matroska 文件。

`extension_correction` 共用此判定，错误后缀输入可提示改为 `.webm`，不强制改名；插件注册会提供打开文件与扫描的后缀列表。插件本身不依赖这些重建版改动，原版使用正确 `.webm/.weba` 后缀即可。原版 EXE 未增加新的文件头提示逻辑，不承诺任意错误后缀均能发现本插件。

## 7. 构建与依据

容器库仅编译 libwebm 的 `mkvparser.cc`，不带 muxer、工具和视频解码器；Opus / Vorbis 静态链接，Release 以 `/O1`、函数节及链接时优化优先缩小 DLL。无需随包分发 FFmpeg、其他解码 DLL 或运行库安装器。

固定来源与许可证见 [ORIGIN](../third_party/ORIGIN.md)。规范依据：[WebM Container Guidelines](https://www.webmproject.org/docs/container/)、[Matroska Codec Specifications](https://www.matroska.org/technical/codec_specs.html)、[Matroska Elements](https://www.matroska.org/technical/elements.html)。规范描述的能力与本项目实际实现范围须区分，以 README 及验证记录为准。
