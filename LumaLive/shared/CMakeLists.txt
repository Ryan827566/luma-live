add_library(luma_shared INTERFACE)
target_include_directories(luma_shared INTERFACE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/contracts ${CMAKE_CURRENT_SOURCE_DIR}/common ${CMAKE_CURRENT_SOURCE_DIR}/runtime-contracts)
target_compile_features(luma_shared INTERFACE cxx_std_20)
