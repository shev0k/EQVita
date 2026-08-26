add_executable(test_eq_shared
  test_eq_shared.c
)

target_include_directories(test_eq_shared PRIVATE
  ${CMAKE_SOURCE_DIR}
)

add_test(NAME eq_shared COMMAND test_eq_shared)

add_executable(test_dsp
  test_dsp.c
  ${CMAKE_SOURCE_DIR}/plugin/dsp.c
)

target_include_directories(test_dsp PRIVATE
  ${CMAKE_SOURCE_DIR}
)

target_link_libraries(test_dsp PRIVATE m)

add_test(NAME dsp COMMAND test_dsp)

add_executable(test_equalizer_apo
  test_equalizer_apo.c
  ${CMAKE_SOURCE_DIR}/app/equalizer_apo.c
  ${CMAKE_SOURCE_DIR}/plugin/dsp.c
)

target_compile_definitions(test_equalizer_apo PRIVATE
  EQVITA_HOST_TESTS=1
  EQVITA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

target_include_directories(test_equalizer_apo PRIVATE
  ${CMAKE_SOURCE_DIR}
)

target_link_libraries(test_equalizer_apo PRIVATE m)

add_test(NAME equalizer_apo COMMAND test_equalizer_apo)

add_executable(test_port_state
  test_port_state.c
)

target_include_directories(test_port_state PRIVATE
  ${CMAKE_SOURCE_DIR}
)

add_test(NAME port_state COMMAND test_port_state)

add_executable(test_port_registry
  test_port_registry.c
  ${CMAKE_SOURCE_DIR}/plugin/dsp.c
)

target_include_directories(test_port_registry PRIVATE
  ${CMAKE_SOURCE_DIR}
)

target_link_libraries(test_port_registry PRIVATE m)

add_test(NAME port_registry COMMAND test_port_registry)

add_executable(test_app_help_text
  test_app_help_text.c
)

target_compile_definitions(test_app_help_text PRIVATE
  EQVITA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

add_test(NAME app_help_text COMMAND test_app_help_text)

add_executable(test_app_state
  test_app_state.c
  ${CMAKE_SOURCE_DIR}/app/app_state.c
)

target_include_directories(test_app_state PRIVATE
  ${CMAKE_SOURCE_DIR}
)

add_test(NAME app_state COMMAND test_app_state)

add_executable(test_app_persistence
  test_app_persistence.c
  ${CMAKE_SOURCE_DIR}/app/persistence.c
)

target_compile_definitions(test_app_persistence PRIVATE
  EQVITA_HOST_TESTS=1
)

target_include_directories(test_app_persistence PRIVATE
  ${CMAKE_SOURCE_DIR}
)

add_test(NAME app_persistence COMMAND test_app_persistence)

add_executable(test_app_persistence_vita_rename
  test_app_persistence_vita_rename.c
)

target_include_directories(test_app_persistence_vita_rename PRIVATE
  ${CMAKE_SOURCE_DIR}
)

add_test(NAME app_persistence_vita_rename COMMAND test_app_persistence_vita_rename)

add_executable(test_app_ui_source
  test_app_ui_source.c
)

target_compile_definitions(test_app_ui_source PRIVATE
  EQVITA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

add_test(NAME app_ui_source COMMAND test_app_ui_source)

add_executable(test_media_browser
  test_media_browser.c
  ${CMAKE_SOURCE_DIR}/app/media_browser.c
)

target_compile_definitions(test_media_browser PRIVATE
  EQVITA_HOST_TESTS=1
)

target_include_directories(test_media_browser PRIVATE
  ${CMAKE_SOURCE_DIR}
)

add_test(NAME media_browser COMMAND test_media_browser)

add_executable(test_media_player_source
  test_media_player_source.c
)

target_compile_definitions(test_media_player_source PRIVATE
  EQVITA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

add_test(NAME media_player_source COMMAND test_media_player_source)

add_executable(test_plugin_source
  test_plugin_source.c
)

target_compile_definitions(test_plugin_source PRIVATE
  EQVITA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

add_test(NAME plugin_source COMMAND test_plugin_source)
