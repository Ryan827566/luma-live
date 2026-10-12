add_library(luma_account_ui STATIC src/AccountView.cpp)
target_include_directories(luma_account_ui PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}/client/account/include
  ${CMAKE_SOURCE_DIR}/shared)
target_link_libraries(luma_account_ui PUBLIC luma_client_account)
target_compile_features(luma_account_ui PUBLIC cxx_std_20)
if(WIN32)
  target_link_libraries(luma_account_ui PRIVATE user32)
endif()
