add_library(luma_media STATIC src/MediaServiceImpl.cpp)
target_include_directories(luma_media PUBLIC include)
target_link_libraries(luma_media PUBLIC luma_shared)
target_compile_features(luma_media PUBLIC cxx_std_20)
