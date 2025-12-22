cmake_minimum_required(VERSION 3.0)

project(EQVita)

set(A_NAME "${PROJECT_NAME}")
set(A_SELF eboot.bin)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -O2 -std=c99")

link_directories(${CMAKE_BINARY_DIR}/plugin/eq_speaker_stubs)
include_directories(${CMAKE_SOURCE_DIR}/common)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/debug_screen)

set(VITA_APP_NAME "EQ Vita")
set(VITA_TITLEID "EQSP00001")
set(VITA_VERSION "01.00")

add_executable("${A_NAME}"
  main.c
  debug_screen/debugScreen.c
)

add_dependencies("${A_NAME}" eq_speaker_stubs)

target_link_libraries("${A_NAME}"
  eqcommon
  EQSpeaker_stub
  SceAppMgr_stub
  SceCtrl_stub
  SceDisplay_stub
  SceLibKernel_stub
  SceIofilemgr_stub
)

vita_create_self("${A_SELF}" "${A_NAME}" UNSAFE)
vita_create_vpk("${A_NAME}.vpk" "${VITA_TITLEID}" "${A_SELF}"
  VERSION "${VITA_VERSION}"
  NAME "${VITA_APP_NAME}"
  FILE sce_sys/icon0.png sce_sys/icon0.png
       sce_sys/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
       sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
       sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
)
