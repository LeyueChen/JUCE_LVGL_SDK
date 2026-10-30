cmake_minimum_required(VERSION 3.25)
# Embed compiler debug information in object files; avoid the shared PDB compiler service.
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>" CACHE STRING "MSVC debug format")
project(LVGL_JUCE_SDK VERSION 0.1.0 LANGUAGES C CXX)
if(MSVC AND DEFINED SDK_MSVC_INCLUDE_PREFIX)
    set(CMAKE_CL_SHOWINCLUDES_PREFIX "${SDK_MSVC_INCLUDE_PREFIX}")
endif()
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(SDK_APP_DIR "${CMAKE_SOURCE_DIR}/user" CACHE PATH "Product source directory")
set(SDK_BOARD windows_sim CACHE STRING "Board")
set(SDK_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated" CACHE PATH "Generated configuration")
find_package(Python COMPONENTS Interpreter REQUIRED)
execute_process(COMMAND "${Python_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/configure.py"
    --app "${SDK_APP_DIR}" --output "${SDK_GENERATED_DIR}" COMMAND_ERROR_IS_FATAL ANY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/Kconfig" "${CMAKE_SOURCE_DIR}/boards/windows_sim/defconfig"
    "${SDK_APP_DIR}/app.conf" "${CMAKE_SOURCE_DIR}/third_party/deps.lock.json")
include("${SDK_GENERATED_DIR}/config.cmake")
if(NOT WIN32 OR NOT SDK_BOARD STREQUAL "windows_sim")
    message(FATAL_ERROR "Only windows_sim is implemented. Embedded providers require a board/BSP integration.")
endif()
add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00 WINVER=0x0A00)
if(MSVC)
    add_compile_options(/utf-8 /Zc:preprocessor)
endif()
include(cmake/Dependencies.cmake)
include(cmake/Components.cmake)
include(third_party/recipes/esp-reference.cmake)
include(cmake/Protocol.cmake)

add_library(sdk_runtime STATIC
    sdk/runtime/core/core.cpp
    sdk/osal/freertos/os.c
    sdk/services/runtime.c
    sdk/adapters/lvgl/display.c
    platforms/windows/devices/storage_backend.cpp
    platforms/windows/core/host_bridge.cpp
    platforms/windows/freertos/runtime.cpp)
target_include_directories(sdk_runtime PUBLIC sdk/include "${SDK_GENERATED_DIR}" PRIVATE platforms/windows/devices)
target_link_libraries(sdk_runtime PUBLIC sdk_freertos lvgl sdk_components)
add_library(sdk::runtime ALIAS sdk_runtime)

add_subdirectory("${SDK_APP_DIR}" "${CMAKE_BINARY_DIR}/user")
add_subdirectory("${SDK_JUCE_DIR}" "${CMAKE_BINARY_DIR}/juce" EXCLUDE_FROM_ALL)
juce_add_gui_app(lvgl_juce PRODUCT_NAME "LVGL JUCE SDK" COMPANY_NAME "LVGL JUCE SDK" BUNDLE_ID "dev.lvgljuce.simulator")
target_sources(lvgl_juce PRIVATE platforms/windows/host_juce/Main.cpp)
target_compile_definitions(lvgl_juce PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=0)
target_link_libraries(lvgl_juce PRIVATE sdk_runtime sdk_user juce::juce_gui_basics juce::juce_recommended_config_flags)
set_target_properties(lvgl_juce PROPERTIES OUTPUT_NAME lvgl_juce RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

enable_testing()
add_executable(sdk_component_tests tests/components.cpp platforms/windows/devices/storage_backend.cpp)
target_include_directories(sdk_component_tests PRIVATE sdk/include platforms/windows/devices "${SDK_GENERATED_DIR}")
target_link_libraries(sdk_component_tests PRIVATE sdk_components)
if(CONFIG_SDK_NANOPB STREQUAL "y")
    sdk_generate_protocol(sdk_test_protocol "${CMAKE_SOURCE_DIR}/tests/protocol" "${SDK_GENERATED_DIR}/test-protocol")
    target_link_libraries(sdk_component_tests PRIVATE sdk_test_protocol)
endif()
add_test(NAME portable_components COMMAND sdk_component_tests "${CMAKE_BINARY_DIR}/test-data")
set_tests_properties(portable_components PROPERTIES TIMEOUT 30)
add_test(NAME configuration_contracts COMMAND "${Python_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/test_config.py")
add_test(NAME runtime_smoke COMMAND lvgl_juce --smoke-seconds 4)
set_tests_properties(runtime_smoke PROPERTIES TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
