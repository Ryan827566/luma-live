add_library(luma_client_signaling STATIC src/SignalingClientService.cpp src/SignalingSession.cpp src/TcpSignalingClient.cpp)
target_include_directories(luma_client_signaling PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../../shared
)
target_compile_features(luma_client_signaling PUBLIC cxx_std_20)
add_executable(luma_client_signaling_tests tests/SignalingClientServiceTests.cpp)
target_link_libraries(luma_client_signaling_tests PRIVATE luma_client_signaling)
add_test(NAME luma_client_signaling_tests COMMAND luma_client_signaling_tests)

if(WIN32)
  target_link_libraries(luma_client_signaling PRIVATE ws2_32)
endif()
