add_library(luma_ai STATIC src/AiClientServiceImpl.cpp)
target_include_directories(luma_ai PUBLIC include)
target_link_libraries(luma_ai PUBLIC luma_shared)
target_compile_features(luma_ai PUBLIC cxx_std_20)
