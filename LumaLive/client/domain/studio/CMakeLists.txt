add_library(luma_studio_domain STATIC
    src/StudioServiceImpl.cpp
    src/SceneGraphImpl.cpp
    src/SceneSelectionServiceImpl.cpp
    src/TransformEditorImpl.cpp
    src/LayerEditorImpl.cpp
    src/PropertyEditorImpl.cpp
    src/SceneCommandHistoryImpl.cpp
    src/SceneEditorImpl.cpp
    src/SceneWorkspaceImpl.cpp)
target_compile_features(luma_studio_domain PUBLIC cxx_std_20)
target_include_directories(luma_studio_domain PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/shared)
target_link_libraries(luma_studio_domain PUBLIC luma_shared)
enable_testing()
add_test(NAME luma_studio_domain_tests COMMAND luma_studio_domain_tests)
add_executable(luma_studio_domain_tests tests/SceneEditorTests.cpp)
target_link_libraries(luma_studio_domain_tests PRIVATE luma_studio_domain)
