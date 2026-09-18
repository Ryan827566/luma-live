add_library(luma_live STATIC src/LiveServiceImpl.cpp)
target_include_directories(luma_live PUBLIC include)
target_link_libraries(luma_live PUBLIC luma_shared)
target_compile_features(luma_live PUBLIC cxx_std_20)
