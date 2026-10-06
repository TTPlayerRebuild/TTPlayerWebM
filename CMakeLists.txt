cmake_minimum_required(VERSION 3.24)
project(TtpWebm LANGUAGES C CXX RC)
if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "ttp_webm requires MSVC and -A Win32")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(cmake/legacy_windows.cmake)
include(cmake/size_release.cmake)
include(cmake/vorbis.cmake)
include(cmake/opus.cmake)
include(cmake/webm.cmake)
include(cmake/version.cmake)
add_library(ttp_webm SHARED src/plugin.cpp src/reader.cpp src/codec.cpp src/document.cpp src/metadata.cpp
  src/ttp_webm.def "${CMAKE_CURRENT_BINARY_DIR}/generated/version.rc")
add_dependencies(ttp_webm ttp_webm_version)
target_include_directories(ttp_webm PUBLIC include PRIVATE src)
target_compile_definitions(ttp_webm PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
  _WIN32_WINNT=0x0501 _CRT_SECURE_NO_WARNINGS)
target_compile_options(ttp_webm PRIVATE /W4 /permissive- /EHsc /GR- /Os /Gy /Gw /GF /utf-8 /fp:precise /arch:SSE2)
target_link_libraries(ttp_webm PRIVATE webm_parser vorbis opus ogg ole32 uuid shlwapi)
target_link_options(ttp_webm PRIVATE /OPT:REF /OPT:ICF /INCREMENTAL:NO /DEBUG:NONE)
set_target_properties(ttp_webm PROPERTIES PREFIX "" OUTPUT_NAME "ttp_webm")
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_command(TARGET ttp_webm POST_BUILD COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_legacy_imports.py" "$<TARGET_FILE:ttp_webm>"
  --exports "${ttplayer_yy_thunks_SOURCE_DIR}/Config/x86/5.1.2600.txt"
  --exports "${ttplayer_yy_thunks_SOURCE_DIR}/Config/x86/6.1.7600.txt"
  --report "$<TARGET_FILE_DIR:ttp_webm>/legacy-imports.json" VERBATIM)
install(TARGETS ttp_webm RUNTIME DESTINATION AddIn COMPONENT Runtime)
