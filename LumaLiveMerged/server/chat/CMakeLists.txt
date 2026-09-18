add_library(luma_chat STATIC src/ChatServiceImpl.cpp)
target_include_directories(luma_chat PUBLIC include)
target_link_libraries(luma_chat PUBLIC luma_shared)
target_compile_features(luma_chat PUBLIC cxx_std_20)
