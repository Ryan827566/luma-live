add_library(luma_auth STATIC src/AuthServiceImpl.cpp)
target_include_directories(luma_auth PUBLIC include)
target_link_libraries(luma_auth PUBLIC luma_shared)
target_compile_features(luma_auth PUBLIC cxx_std_20)
