add_executable(luma_studio WIN32 src/main.cpp)
target_compile_features(luma_studio PRIVATE cxx_std_20)
target_include_directories(luma_studio PRIVATE ${CMAKE_SOURCE_DIR}/client/ui/account/include)
target_link_libraries(luma_studio PRIVATE
  luma_shared
  luma_studio_domain
  luma_studio_application
  luma_scene_editor_ui
  luma_studio_shell
  luma_studio_preview
  luma_account_ui
  $<$<PLATFORM_ID:Windows>:user32 gdi32>
)
