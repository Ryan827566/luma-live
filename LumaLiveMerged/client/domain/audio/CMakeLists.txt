add_library(luma_audio STATIC src/AudioServiceImpl.cpp)
target_include_directories(luma_audio PUBLIC include)
target_link_libraries(luma_audio PUBLIC luma_shared)
target_compile_features(luma_audio PUBLIC cxx_std_20)
