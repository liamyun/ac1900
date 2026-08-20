set(LOGIN_HELPER_SOURCES
  main.cpp main_window.cpp main_window.h client_handler.cc client_handler.h
  config.cpp config.h
  network_capture.cpp network_capture.h market_data.cpp market_data.h
  mail_service.cpp mail_service.h scheduler.cpp scheduler.h util.cpp util.h
  ui_messages.h resource.h resources/PersonalCEF.rc
)
add_executable(PersonalCEFLogin WIN32 ${LOGIN_HELPER_SOURCES})
SET_EXECUTABLE_TARGET_PROPERTIES(PersonalCEFLogin)
target_compile_features(PersonalCEFLogin PRIVATE cxx_std_20)
target_compile_definitions(PersonalCEFLogin PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(PersonalCEFLogin PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}")
add_dependencies(PersonalCEFLogin libcef_dll_wrapper)
# Do not link the cefclient-local logical target `libcef_lib` here.
# In generated VS projects that name may degrade to a literal libcef_lib.lib.
# Link the real CEF import library for each configuration instead.
target_link_libraries(PersonalCEFLogin PRIVATE
  "$<$<CONFIG:Debug>:${CEF_ROOT}/Debug/libcef.lib>"
  "$<$<CONFIG:Release>:${CEF_ROOT}/Release/libcef.lib>"
  libcef_dll_wrapper
  ${CEF_STANDARD_LIBS}
  comctl32 shell32 ole32 advapi32 dbghelp
)
set_target_properties(PersonalCEFLogin PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY_DEBUG "${PERSONALCEF_OUTPUT_DIR}/Debug"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${PERSONALCEF_OUTPUT_DIR}/Release"
  VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:PersonalCEFLogin>"
)
COPY_FILES("PersonalCEFLogin" "${CEF_BINARY_FILES}" "${CEF_BINARY_DIR}" "${PERSONALCEF_OUTPUT_DIR}/$<CONFIGURATION>")
COPY_FILES("PersonalCEFLogin" "${CEF_RESOURCE_FILES}" "${CEF_RESOURCE_DIR}" "${PERSONALCEF_OUTPUT_DIR}/$<CONFIGURATION>")
