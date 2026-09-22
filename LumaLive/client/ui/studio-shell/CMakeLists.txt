add_library(luma_studio_shell STATIC
  src/StudioShell.cpp
  src/StudioPageRouter.cpp
)
target_include_directories(luma_studio_shell PUBLIC include)
target_compile_features(luma_studio_shell PUBLIC cxx_std_20)
target_compile_options(luma_studio_shell PRIVATE /utf-8 /W4 /permissive-)
target_link_libraries(luma_studio_shell PUBLIC user32 gdi32 dwmapi)
