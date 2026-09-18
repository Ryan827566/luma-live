add_executable(luma_admin src/main.cpp)
target_compile_features(luma_admin PRIVATE cxx_std_20)
target_link_libraries(luma_admin PRIVATE luma_shared)
