FetchContent_Declare(ttplayer_webm
  URL https://codeload.github.com/webmproject/libwebm/zip/refs/tags/libwebm-1.0.0.32
  URL_HASH SHA256=8f6503acd1607e85780d902e9cdb4a5ba5e30034c954b884734932d854838151
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SOURCE_SUBDIR unused)
FetchContent_MakeAvailable(ttplayer_webm)
# Build the upstream demuxer only, without muxers, tools or video decoders.
add_library(webm_parser STATIC "${ttplayer_webm_SOURCE_DIR}/mkvparser/mkvparser.cc")
target_include_directories(webm_parser PUBLIC "${ttplayer_webm_SOURCE_DIR}")
target_compile_definitions(webm_parser PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(webm_parser PRIVATE /Os /Gy /Gw /GF /EHsc /GR- /arch:SSE2 /wd4244 /wd4267 /wd4996)
