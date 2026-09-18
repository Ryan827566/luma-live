add_executable(luma_server src/main.cpp)
target_compile_features(luma_server PRIVATE cxx_std_20)
target_link_libraries(luma_server PRIVATE luma_shared luma_signaling)
