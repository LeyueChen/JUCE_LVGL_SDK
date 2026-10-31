include_guard(GLOBAL)

function(sdk_configure_lvgl project_directory generated_directory)
    get_filename_component(config_directory "${project_directory}/config" ABSOLUTE)
    set(config_file "${config_directory}/lv_conf.h")
    if(NOT EXISTS "${config_file}" OR IS_DIRECTORY "${config_file}")
        message(FATAL_ERROR "Project must provide its own LVGL configuration: ${config_file}. Copy config/lv_conf.h from templates/project when creating a project.")
    endif()
    file(REAL_PATH "${project_directory}" project_root)
    file(REAL_PATH "${config_file}" resolved_config)
    cmake_path(IS_PREFIX project_root "${resolved_config}" NORMALIZE inside_project)
    if(NOT inside_project)
        message(FATAL_ERROR "LVGL configuration must stay inside its project: ${config_file}")
    endif()
    # COPYONLY preserves native LVGL macros and registers the input as a
    # configure dependency. The generated path stays private to this build.
    configure_file("${config_file}" "${generated_directory}/lv_conf.h" COPYONLY)
    set(SDK_LVGL_CONFIG_FILE "${config_file}" CACHE INTERNAL "Selected project's LVGL configuration" FORCE)
    set(SDK_LVGL_CONFIG_DIR "${config_directory}" PARENT_SCOPE)
endfunction()

function(sdk_configure_lvgl_build)
    set(options_file "${SDK_GENERATED_DIR}/lvgl-options.cmake")
    set(probe_args "")
    string(TOUPPER "${CMAKE_BUILD_TYPE}" configuration)
    separate_arguments(compiler_flags NATIVE_COMMAND "${CMAKE_C_FLAGS} ${CMAKE_C_FLAGS_${configuration}}")
    foreach(flag IN LISTS compiler_flags)
        list(APPEND probe_args "--compiler-arg=${flag}")
    endforeach()
    get_directory_property(definitions COMPILE_DEFINITIONS)
    foreach(definition IN LISTS definitions)
        list(APPEND probe_args "--define=${definition}")
    endforeach()
    execute_process(COMMAND "${Python_EXECUTABLE}" "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../tools/lvgl_config.py"
        --compiler "${CMAKE_C_COMPILER}" --compiler-id "${CMAKE_C_COMPILER_ID}"
        --lvgl "${SDK_LVGL_DIR}" --config "${SDK_GENERATED_DIR}/lv_conf.h"
        --include "${SDK_GENERATED_DIR}" --include "${SDK_LVGL_CONFIG_DIR}"
        --include "${SDK_LVGL_DIR}/include" --include "${SDK_LVGL_DIR}"
        --output "${options_file}" ${probe_args}
        COMMAND_ERROR_IS_FATAL ANY)
    include("${options_file}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${SDK_LVGL_CONFIG_DEPENDENCIES})
    # Derived settings replace old cache entries on every configure. The
    # project's preprocessed header is the only source of these switches.
    foreach(variable IN LISTS SDK_LVGL_BUILD_OPTIONS)
        set(${variable} "${${variable}}" CACHE INTERNAL "Derived from project lv_conf.h" FORCE)
    endforeach()
    # This SDK provides its own host and dependency adapters. Optional native
    # backends requiring other libraries must be integrated explicitly first.
    foreach(option DRAW_G2D DRAW_SDL SDL LINUX_DRM WAYLAND X11 GLFW NUTTX_LIBUV
                   EVDEV LIBINPUT LIBWEBP GLTF GSTREAMER FFMPEG LIBJPEG_TURBO LIBPNG FREETYPE)
        if(CONFIG_LV_USE_${option})
            message(FATAL_ERROR "LV_USE_${option}=1 in ${SDK_LVGL_CONFIG_FILE} requires an external backend/library not integrated by this SDK. Disable it or add an explicitly reviewed dependency provider.")
        endif()
    endforeach()
    if(CONFIG_LV_USE_THORVG AND NOT CONFIG_LV_USE_THORVG_INTERNAL)
        message(FATAL_ERROR "External ThorVG is not integrated. Set LV_USE_THORVG_INTERNAL=1 in ${SDK_LVGL_CONFIG_FILE} or disable LV_USE_THORVG.")
    endif()
    foreach(demo FLEX_LAYOUT MULTILANG TRANSFORM SCROLL EBIKE HIGH_RES SMARTWATCH)
        if(CONFIG_LV_USE_DEMO_${demo})
            message(FATAL_ERROR "LV_USE_DEMO_${demo}=1 requires the external lv_demos repository, which is not integrated by this SDK.")
        endif()
    endforeach()
endfunction()
