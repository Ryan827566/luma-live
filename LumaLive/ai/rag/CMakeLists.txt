add_library(luma_rag STATIC src/RagStoreImpl.cpp)
target_include_directories(luma_rag PUBLIC include)
target_link_libraries(luma_rag PUBLIC luma_shared)
target_compile_features(luma_rag PUBLIC cxx_std_20)
