add_library(${SDK_PROJECT_ID}_cores STATIC core_roles.c core_cpu_0.c core_cpu_1.c core_cpu_2.c core_cpu_3.c)
target_include_directories(${SDK_PROJECT_ID}_cores PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(${SDK_PROJECT_ID}_cores PUBLIC sdk::runtime)
set_property(DIRECTORY PROPERTY SDK_USER_MODULE_TARGET ${SDK_PROJECT_ID}_cores)
