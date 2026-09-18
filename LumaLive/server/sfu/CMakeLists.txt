add_library(luma_sfu STATIC src/SfuServiceImpl.cpp)
target_include_directories(luma_sfu PUBLIC include)
target_link_libraries(luma_sfu PUBLIC luma_shared)
target_compile_features(luma_sfu PUBLIC cxx_std_20)
