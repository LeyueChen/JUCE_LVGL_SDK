add_library(${SDK_PROJECT_ID}_media STATIC media.c)
target_include_directories(${SDK_PROJECT_ID}_media PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(${SDK_PROJECT_ID}_media PUBLIC sdk::runtime)
set_property(DIRECTORY PROPERTY SDK_USER_MODULE_TARGET ${SDK_PROJECT_ID}_media)
