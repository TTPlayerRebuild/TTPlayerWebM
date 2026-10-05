# 第三方来源

源码由 CMake 在构建时下载；仓库仅保留本文和授权文本。所有下载均固定版本与 SHA-256。

| 项目 | 固定下载地址 | SHA-256 |
| --- | --- | --- |
| libwebm 1.0.0.32 | https://codeload.github.com/webmproject/libwebm/zip/refs/tags/libwebm-1.0.0.32 | `8f6503acd1607e85780d902e9cdb4a5ba5e30034c954b884734932d854838151` |
| libogg 1.3.6 | https://downloads.xiph.org/releases/ogg/libogg-1.3.6.tar.xz | `5c8253428e181840cd20d41f3ca16557a9cc04bad4a3d04cce84808677fa1061` |
| libvorbis 1.3.7 | https://downloads.xiph.org/releases/vorbis/libvorbis-1.3.7.tar.xz | `b33cc4934322bcbf6efcbacf49e3ca01aadbea4114ec9589d1b1e9d20f72954b` |
| libopus 1.6.1 | https://downloads.xiph.org/releases/opus/opus-1.6.1.tar.gz | `6ffcb593207be92584df15b32466ed64bbec99109f007c82205f0194572411a1` |
| YY-Thunks 1.2.2 | https://github.com/Chuyu-Team/YY-Thunks/releases/download/v1.2.2/YY-Thunks-Objs.zip | `518ed7ef4825e8a41997fbccfa2c8090cf31a6038fd51520a2e49886f947f9fc` |
| VC-LTL 5.3.1 | https://github.com/Chuyu-Team/VC-LTL5/releases/download/v5.3.1/VC-LTL-Binary.7z | `7a18799ed3aa84a225610a5447a56bc534c5c98ccb8dec05caba0e3f633431ad` |

上游源码不纳入仓库；构建缓存中的源代码未经修改。libwebm 固定 tag 对应提交 `f2a982d748b80586ae53b89a2e6ebbc305848b8c`，仅编译 mkvparser。Vorbis 由 libvorbis 直接处理包，不使用 vorbisfile；Opus 由 libopus 直接解码，不使用 opusfile。

构建关闭上游测试、工具和共享库，保留标准解码；关闭 Opus DRED / OSCE 及额外 SIMD 代码生成，以 SSE2 为旧 Windows CPU 基线。VC-LTL 5.3.1 和 YY-Thunks 1.2.2 使用 XP 目标，二者均是构建依赖。

许可证原文：

- [libwebm LICENSE](libwebm/LICENSE.TXT)、[PATENTS](libwebm/PATENTS.TXT)、[AUTHORS](libwebm/AUTHORS.TXT)
- [libogg COPYING](libogg/COPYING)
- [libvorbis COPYING](libvorbis/COPYING)
- [libopus COPYING（含专利声明链接）](opus/COPYING)
- [VC-LTL](../docs/licenses/VC-LTL-LICENSE.txt)
- [YY-Thunks](../docs/licenses/YY-Thunks-LICENSE.txt)

版权与许可证保留在仓库，发行脚本把第三方声明附于 Release 说明，不嵌入 DLL；ZIP 仅含插件和校验文件。单独再分发二进制时，应一并提供本目录对应的声明或发行说明。

官方来源：[libwebm](https://github.com/webmproject/libwebm)、[Xiph 下载](https://xiph.org/downloads/)、[Opus 下载](https://opus-codec.org/downloads/)、[VC-LTL](https://github.com/Chuyu-Team/VC-LTL5)、[YY-Thunks](https://github.com/Chuyu-Team/YY-Thunks)。libogg / libvorbis / libopus 沿用 ttp_ogg 在 2026-10-03 核实的固定版本；libwebm 在 2026-10-06 核实并校验下载。构建不自动追踪可变的 latest 分支。
