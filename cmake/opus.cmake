# Latest stable upstream releases checked on 2026-10-03. Sources stay in the
# ignored build cache; every network download is pinned by SHA-256.
FetchContent_Declare(ttplayer_opus
  URL https://downloads.xiph.org/releases/opus/opus-1.6.1.tar.gz
  URL_HASH SHA256=6ffcb593207be92584df15b32466ed64bbec99109f007c82205f0194572411a1
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
foreach(feature BUILD_TESTING BUILD_PROGRAMS BUILD_SHARED_LIBRARY DRED OSCE
    INSTALL_PKG_CONFIG_MODULE INSTALL_CMAKE_CONFIG_MODULE)
  set(OPUS_${feature} OFF CACHE BOOL "Small static decoder distribution" FORCE)
endforeach()
# One portable path avoids AVX/OSXSAVE requirements on legacy Windows. Link-time
# elimination removes the unused encoder; standard Opus decoding stays enabled.
set(OPUS_DISABLE_INTRINSICS ON CACHE BOOL "Portable SSE2 baseline" FORCE)
set(OPUS_STATIC_RUNTIME ON CACHE BOOL "VC-LTL runtime" FORCE)
FetchContent_MakeAvailable(ttplayer_opus)
target_compile_options(opus PRIVATE /Os /Gy /Gw /GF /fp:precise /arch:SSE2 /wd4244 /wd4267 /wd4996)
