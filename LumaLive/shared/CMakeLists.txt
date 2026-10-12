add_library(luma_shared INTERFACE)
target_include_directories(luma_shared INTERFACE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/contracts ${CMAKE_CURRENT_SOURCE_DIR}/common ${CMAKE_CURRENT_SOURCE_DIR}/runtime-contracts)
target_compile_features(luma_shared INTERFACE cxx_std_20)

add_library(luma_auth_crypto STATIC contracts/auth/AuthCrypto.cpp)
target_link_libraries(luma_auth_crypto PUBLIC luma_shared)
if(WIN32)
  target_link_libraries(luma_auth_crypto PRIVATE bcrypt)
endif()
