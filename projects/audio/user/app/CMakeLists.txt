add_library(audio_app STATIC app.c)
target_link_libraries(audio_app PUBLIC sdk::runtime rakure::ea_sdk rakure::gui)
set_property(DIRECTORY PROPERTY SDK_USER_MODULE_TARGET audio_app)
