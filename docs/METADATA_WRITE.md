# WebM 标签、封面写入

实现版本：`2026.10.06p1`。本项目沿用原版 Reader / Metadata / Thumbnail 接口；WebM 是新增容器支持，不存在原版 WebM 写入代码可直接还原。

## 1. 宿主接口

原版在编辑期间通过 Metadata / Thumbnail 修改内存对象，最后一次释放 Reader 时写盘。接口的 Release 返回引用计数，不能返回提交 HRESULT。因此本插件保留这一提交时机，并在失败时输出诊断；不能承诺原版“保存成功”界面代表写盘成功。

重建版已有可选 `MetadataCommit` 接口，GUID 为 `{3ab643c1-d8a4-4d49-9cbb-ef196b04b6e7}`，文件属性工作线程会主动调用 Commit 并处理 HRESULT。本插件实现相同扩展，无需改变原版虚表。Commit 为终结操作，成功或失败后重复调用返回首次结果，Release 不再次尝试保存，后续修改和解码被拒绝。

能识别可编辑文件时，能力位开放定位、标签写入、封面写入；只读文件、网络 URL、缺少路径或不安全布局不开放修改。能力位只表示实现支持，权限、磁盘空间、文件竞争等实际提交错误仍可能在保存时返回。

## 2. 标签映射及作用范围

| 宿主字段 | WebM SimpleTag 名称 |
| --- | --- |
| Title / Artist / Album / Genre / Comment | TITLE / ARTIST / ALBUM / GENRE / COMMENT |
| Year / Date | DATE_RELEASED |
| Track / TrackNumber | PART_NUMBER |
| AlbumArtist | ALBUM_ARTIST |

其它可打印 ASCII 名称转大写后保存。字段名不超过 1024 字符，值不超过 1 MiB UTF-8。未编辑的多值、语言、二进制及嵌套标签保留；对一个字段赋值替换该字段的旧值，空值删除该字段；`Set(nullptr, nullptr)` 清空所选音轨可见标签。

读取按字段应用“所选 TrackUID 优先于全局”规则，排除其它音轨、章节、Edition、附件和块扩展的标签。修改继承的全局字段时，将受影响旧值保留给其它音轨，避免修改当前歌曲时污染其它音轨；其它音轨已有专属值时不把继承值提升到同一优先级。未修改的全局字段保持原作用范围。

Title 不存在时仍可回退 Track Name / Segment Title。用户显式删除标题时写入空的轨道 TITLE，抑制回退，避免“删除后重新出现”或改掉其它轨道标题。

## 3. 封面读写

支持 PNG / JPEG 原始图片字节，无转码；添加、替换、删除和类型/描述修改复用原版 Thumbnail。ReplacePicture 的字段掩码为 MIME=1、描述=2、类型=4、数据=8。每张最多 16 MiB，最多 64 张，图片类型 0～20。PNG 检查块边界、尺寸、CRC 和 IEND；JPEG 检查结构、尺寸及首尾标志。这些检查不能代替完整图像解码器对所有损坏图像的识别。

新增正面封面使用 `cover.png` / `cover.jpg`，背面使用 `back-<UID>`；其它类型采用 `ttplayer-cover-<type>-<UID>` 文件名保留类型。描述存入 FileDescription；空描述保持为空。替换时保留 FileUID 和未知附件字段，新增使用非零唯一 UID。

只移除本插件识别的封面；字体、文本等无关附件及未识别图片保留。删除封面时清理针对已删除 FileUID 的标签；多附件目标只删除对应 UID，保留其它附件和 UID=0 的“全部附件”目标。保存输出最多一个 Attachments 元素。

**兼容性边界：** WebM 的容器指南将 Attachments 列为不支持，而 Matroska 元素表及现实工具存在附件支持。此处作为 Matroska 兼容扩展提供，原版与重建版可经插件读取，不意味着所有浏览器、WebM 校验器或其它播放器均接受内嵌封面。仅修改标签不会自动增加封面附件。

## 4. 保存算法

1. 核对路径、文件标识、大小和最后修改时间，拒绝已检测到的外部修改。
2. 在原文件目录创建随机临时文件。保留 EBML 头、Tracks、Info、Cues、音视频 Cluster 和未知顶层元素；旧元数据及 SeekHead 在原位置变为等长 Void。
3. 将更新的 Tags / Attachments 和新 SeekHead 放在尾部；优先复用前部旧 SeekHead 或足够大的 Void 放置索引指针。没有可用位置时保留尾部完整索引，插件扫描仍可发现。
4. 保持媒体数据相对 Segment 的偏移不变，因此原 Cues 仍有效。Segment 长度域不足时仅扩宽其头部；有限文件中的未知长度 Cluster 在原长度域宽度内封口，无法容纳则拒绝写入。裁掉可回收的尾部旧元数据/Void，连续相同修改不会无限增长。
5. 重算被编辑层级已有的 CRC；重新打开暂存文件，验证音轨格式、帧数、包数及目标标签和封面。
6. 创建完整原文件备份，再次检查文件身份。能关闭宿主源句柄时尝试 ReplaceFileW；受共享句柄限制时，通过可写流写回并设置长度、Commit。
7. 写回失败则从备份恢复。恢复也失败时保留原文件备份和已校验新文件，通过 OutputDebugString 输出两者路径，供人工恢复。

完整复制避免重新编码音视频，但保存大型文件需要时间与磁盘空间。最高阶段会同时存在原文件、完整暂存新文件、完整备份。元数据序列化单缓冲限制 64 MiB，单层子元素不超过 16384 个，嵌套 SimpleTag 最多 8 层。特殊签名布局、无法无损处理的结构或超限文件返回错误，不承诺任意 Matroska 变体均可写入。

## 5. 失败保护的范围

- 短写、读取失败、Commit 失败、只读文件、外部大小/时间/身份变化返回失败；回归测试验证可恢复错误不改变原文件。
- 没有修改时不重写文件。失败的显式 Commit 不会在 Release 时偷偷重试。
- 原流回写不是文件系统事务，断电、进程被强行终止或存储硬件故障可能中断写回；不能将备份回滚描述为断电原子性。
- 文件身份检查不是全文件锁或内容哈希，不能保证阻止所有同时进行且尚未刷新时间戳的外部写入。
- 原版没有提交结果返回通道；重建版可以报告保存失败。保存失败时应保留源文件及恢复文件，不应把 UI 已关闭作为成功依据。

## 6. XP 修正

XP 实测发现 PNG 写入与修改返回 E_FAIL，Win7/Win11 正常。原因位于 CRC 查表的函数局部动态静态初始化路径：现代 MSVC 的 TLS 初始化保护不适合 XP 动态加载 DLL。CRC 表现已为编译期 `constexpr std::array`，无初始化锁、堆分配或线程局部状态。XP 的 PNG 新增、修改，以及 Tags / Attachments / Segment CRC 保存已重新验证。

## 参考

- [WebM Container Guidelines](https://www.webmproject.org/docs/container/)
- [Matroska Elements](https://www.matroska.org/technical/elements.html)
- [Matroska Attachments](https://www.matroska.org/technical/attachments.html)
- [RFC 8794：EBML、CRC 与 Void](https://www.rfc-editor.org/rfc/rfc8794.html)
