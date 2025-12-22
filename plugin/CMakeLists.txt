cmake_minimum_required(VERSION 3.0)

project(eq_speaker_plugin)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -nostdlib -Wall -Wextra -O2 -std=gnu99 -mtune=cortex-a9")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -nostdlib")

add_executable(eq_speaker
  main.c
  dsp.c
)

target_link_libraries(eq_speaker
  eqcommon
  SceIofilemgrForDriver_stub
  SceSysmemForDriver_stub
  SceThreadmgrForDriver_stub
  SceSysclibForDriver_stub
  SceCtrlForDriver_stub
  taihenForKernel_stub
  taihenModuleUtils_stub
  m
  gcc
)

vita_create_self(eq_speaker.skprx eq_speaker CONFIG exports.yml UNSAFE)
vita_create_stubs(eq_speaker_stubs eq_speaker.skprx exports.yml KERNEL)
