add_library(luma_im STATIC src/ImServiceImpl.cpp)
target_include_directories(luma_im PUBLIC include)
target_link_libraries(luma_im PUBLIC luma_shared)
target_compile_features(luma_im PUBLIC cxx_std_20)
