add_library(${SDK_PROJECT_ID}_app STATIC app.c)
target_include_directories(${SDK_PROJECT_ID}_app PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(${SDK_PROJECT_ID}_app PUBLIC sdk::runtime ${SDK_PROJECT_ID}_media)
set_property(DIRECTORY PROPERTY SDK_USER_MODULE_TARGET ${SDK_PROJECT_ID}_app)
