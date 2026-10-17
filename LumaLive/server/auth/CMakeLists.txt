add_library(luma_auth STATIC src/AuthServiceImpl.cpp src/AuthStore.cpp src/SmsProvider.cpp src/EmailProvider.cpp)
target_include_directories(luma_auth PUBLIC include ${CMAKE_SOURCE_DIR}/shared)
target_link_libraries(luma_auth PUBLIC luma_auth_crypto)
target_compile_features(luma_auth PUBLIC cxx_std_20)
if(WIN32)
  target_link_libraries(luma_auth PUBLIC ws2_32 winhttp)
elseif(UNIX AND NOT APPLE)
  target_link_libraries(luma_auth PUBLIC dl)
endif()

add_executable(luma_auth_unit_tests tests/AuthServiceTests.cpp)
target_link_libraries(luma_auth_unit_tests PRIVATE luma_auth)
target_compile_features(luma_auth_unit_tests PRIVATE cxx_std_20)
add_test(NAME luma_auth_unit_tests COMMAND luma_auth_unit_tests)

add_executable(luma_auth_integration_tests tests/AuthIntegrationTests.cpp)
target_link_libraries(luma_auth_integration_tests PRIVATE luma_auth luma_client_account)
target_compile_features(luma_auth_integration_tests PRIVATE cxx_std_20)
add_test(NAME luma_auth_integration_tests COMMAND luma_auth_integration_tests)

add_executable(luma_sms_provider_tests tests/SmsProviderTests.cpp)
target_link_libraries(luma_sms_provider_tests PRIVATE luma_auth)
target_compile_features(luma_sms_provider_tests PRIVATE cxx_std_20)
add_test(NAME luma_sms_provider_tests COMMAND luma_sms_provider_tests)

add_executable(luma_email_provider_tests tests/EmailProviderTests.cpp)
target_link_libraries(luma_email_provider_tests PRIVATE luma_auth)
target_compile_features(luma_email_provider_tests PRIVATE cxx_std_20)
add_test(NAME luma_email_provider_tests COMMAND luma_email_provider_tests)

add_executable(luma_auth_store_row_mutation_tests tests/AuthStoreRowMutationTests.cpp)
target_link_libraries(luma_auth_store_row_mutation_tests PRIVATE luma_auth)
target_compile_features(luma_auth_store_row_mutation_tests PRIVATE cxx_std_20)
add_test(NAME luma_auth_store_row_mutation_tests COMMAND luma_auth_store_row_mutation_tests)

# These regression executables use assert for both actions and assertions.
# Keep them active in Release, where the shipped client is verified.
foreach(test_target luma_auth_unit_tests luma_auth_integration_tests luma_sms_provider_tests luma_email_provider_tests luma_auth_store_row_mutation_tests)
  if(MSVC)
    target_compile_options(${test_target} PRIVATE /UNDEBUG /utf-8)
  else()
    target_compile_options(${test_target} PRIVATE -UNDEBUG)
  endif()
  set_tests_properties(${test_target} PROPERTIES TIMEOUT 120)
  set_tests_properties(${test_target} PROPERTIES ENVIRONMENT "LUMALIVE_AUTH_ENV=test;LUMALIVE_AUTH_TLS_CA_FILE=;LUMALIVE_AUTH_TLS_CERT_FILE=;LUMALIVE_AUTH_TLS_KEY_FILE=")
endforeach()

if(WIN32)
  add_executable(luma_auth_tls_fixture tests/AuthTlsFixture.cpp)
  target_link_libraries(luma_auth_tls_fixture PRIVATE luma_auth luma_client_account)
  target_compile_features(luma_auth_tls_fixture PRIVATE cxx_std_20)
  target_compile_options(luma_auth_tls_fixture PRIVATE /utf-8)
  get_filename_component(_luma_repository "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
  set(LUMALIVE_AUTH_TLS_LIBRARY_DIR "${_luma_repository}/output/services/postgresql/runtime/pgsql/bin" CACHE PATH
      "Absolute directory containing the packaged OpenSSL 3 runtime for TLS tests")
  find_package(Python3 COMPONENTS Interpreter QUIET)
  find_program(LUMALIVE_AUTH_TEST_OPENSSL openssl HINTS "C:/Program Files/Git/usr/bin")
  if(Python3_Interpreter_FOUND AND LUMALIVE_AUTH_TEST_OPENSSL AND EXISTS "${LUMALIVE_AUTH_TLS_LIBRARY_DIR}/libssl-3-x64.dll")
    add_test(NAME luma_auth_tls_tests COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_auth_tls.py"
      --fixture "$<TARGET_FILE:luma_auth_tls_fixture>" --openssl "${LUMALIVE_AUTH_TEST_OPENSSL}"
      --library-dir "${LUMALIVE_AUTH_TLS_LIBRARY_DIR}")
    set_tests_properties(luma_auth_tls_tests PROPERTIES TIMEOUT 180)
  endif()
endif()
