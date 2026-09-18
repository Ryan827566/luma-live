add_library(luma_model STATIC src/ModelAdapterImpl.cpp)
target_include_directories(luma_model PUBLIC include)
target_link_libraries(luma_model PUBLIC luma_shared)
target_compile_features(luma_model PUBLIC cxx_std_20)
