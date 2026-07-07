cmake_minimum_required(VERSION 3.16)

project(EQVita)

set(A_NAME "${PROJECT_NAME}")
set(A_SELF eboot.bin)

set(VITA_APP_NAME "EQ Vita")
set(VITA_TITLEID "EQSP00001")
set(VITA_VERSION "01.14")

add_executable("${A_NAME}"
  main.c
  app_state.c
  media_browser.c
  media_player.c
  persistence.c
  ui_vita.c
)

add_dependencies("${A_NAME}" eq_speaker_stubs)

target_compile_options("${A_NAME}" PRIVATE
  -Wall
  -Wextra
  -std=c99
)

target_link_directories("${A_NAME}" PRIVATE
  ${CMAKE_BINARY_DIR}/plugin/eq_speaker_stubs
)

target_include_directories("${A_NAME}" PRIVATE
  ${CMAKE_SOURCE_DIR}/common
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries("${A_NAME}"
  eqcommon
  EQSpeaker_stub
  vita2d
  SceAVConfig_stub
  SceAudio_stub
  SceAudiodec_stub
  SceAppUtil_stub
  SceCtrl_stub
  SceTouch_stub
  SceDisplay_stub
  SceAppMgr_stub
  SceGxm_stub
  SceCommonDialog_stub
  ScePgf_stub
  ScePvf_stub
  SceSysmodule_stub
  SceLibKernel_stub
  SceIofilemgr_stub
  freetype
  vorbisfile
  vorbis
  ogg
  mpg123
  png
  jpeg
  z
  m
)

vita_create_self("${A_SELF}" "${A_NAME}" UNSAFE)
vita_create_vpk("${A_NAME}.vpk" "${VITA_TITLEID}" "${A_SELF}"
  VERSION "${VITA_VERSION}"
  NAME "${VITA_APP_NAME}"
  FILE sce_sys/icon0.png sce_sys/icon0.png
       assets/ui_icons_vita_teal.png assets/ui_icons_vita_teal.png
       assets/ui_icons_graphite.png assets/ui_icons_graphite.png
       assets/ui_icons_amoled_cyan.png assets/ui_icons_amoled_cyan.png
       assets/ui_icons_nord_frost.png assets/ui_icons_nord_frost.png
       assets/ui_icons_midnight_blue.png assets/ui_icons_midnight_blue.png
       assets/ui_icons_mocha_pastel.png assets/ui_icons_mocha_pastel.png
       assets/ui_icons_dracula_bloom.png assets/ui_icons_dracula_bloom.png
       assets/ui_icons_tokyo_neon.png assets/ui_icons_tokyo_neon.png
       assets/ui_icons_solar_amber.png assets/ui_icons_solar_amber.png
       assets/ui_icons_rose_pine.png assets/ui_icons_rose_pine.png
       assets/ui_icons_aurora_light.png assets/ui_icons_aurora_light.png
       assets/ui_icons_high_contrast.png assets/ui_icons_high_contrast.png
       assets/ui_icons_red_onyx.png assets/ui_icons_red_onyx.png
       assets/ui_icons_crimson_vita.png assets/ui_icons_crimson_vita.png
       assets/ui_icons_ember_core.png assets/ui_icons_ember_core.png
       assets/ui_icons_ruby_noir.png assets/ui_icons_ruby_noir.png
       assets/THIRD_PARTY_NOTICES.md assets/THIRD_PARTY_NOTICES.md
       sce_sys/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
       sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
       sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
)

if(TARGET "${A_NAME}.vpk-vpk" AND TARGET "${A_SELF}-self")
  add_dependencies("${A_NAME}.vpk-vpk" "${A_SELF}-self")
endif()
