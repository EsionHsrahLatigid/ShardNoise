cmake_minimum_required(VERSION 3.31)

if(APPLE)
    project(ShardNoise VERSION 0.2.0 LANGUAGES C CXX OBJC OBJCXX)
else()
    project(ShardNoise VERSION 0.2.0 LANGUAGES C CXX)
endif()

option(SHARDNOISE_BUILD_PLUGIN "Build the YUP Standalone/VST3/AU targets" ON)
option(SHARDNOISE_BUILD_TESTS "Build deterministic DSP regression tests" ON)
option(SHARDNOISE_ADHOC_SIGN "Ad-hoc sign local macOS plugin and app bundles after building" ON)
set(SHARDNOISE_YUP_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../yup" CACHE PATH
    "Path to a local YUP checkout; fetched from upstream when unavailable")

add_library(shardnoise_engine STATIC
    source/violent/ShardNoiseEngine.cpp)
target_include_directories(shardnoise_engine PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/include")
target_compile_features(shardnoise_engine PUBLIC cxx_std_20)
set_target_properties(shardnoise_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(shardnoise_engine PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(SHARDNOISE_BUILD_TESTS)
    include(CTest)

    function(enable_test_assertions target_name)
        if(MSVC)
            target_compile_options(${target_name} PRIVATE /UNDEBUG)
        elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
            target_compile_options(${target_name} PRIVATE -UNDEBUG)
        endif()
    endfunction()

    add_executable(shardnoise_engine_tests
        tests/ShardNoiseEngineTests.cpp)
    target_link_libraries(shardnoise_engine_tests PRIVATE shardnoise_engine)
    target_compile_features(shardnoise_engine_tests PRIVATE cxx_std_20)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(shardnoise_engine_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    enable_test_assertions(shardnoise_engine_tests)
    add_test(NAME shardnoise_engine_tests COMMAND shardnoise_engine_tests)
endif()

if(SHARDNOISE_BUILD_PLUGIN)
    set(YUP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(YUP_BUILD_PLUGINS OFF CACHE BOOL "" FORCE)
    set(YUP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(YUP_ENABLE_PLUGINVAL OFF CACHE BOOL "" FORCE)
    set(YUP_ENABLE_VST3_VALIDATOR OFF CACHE BOOL "" FORCE)
    set(YUP_ENABLE_CLAP_VALIDATOR OFF CACHE BOOL "" FORCE)
    set(YUP_ENABLE_AUVAL_VALIDATOR OFF CACHE BOOL "" FORCE)

    if(EXISTS "${SHARDNOISE_YUP_SOURCE_DIR}/CMakeLists.txt")
        add_subdirectory("${SHARDNOISE_YUP_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/yup")
    else()
        include(FetchContent)
        FetchContent_Declare(yup
            GIT_REPOSITORY https://github.com/kunitoki/yup.git
            GIT_TAG 9a1c9bc699b6a714f6f52486462d98a140c8bf95)
        FetchContent_MakeAvailable(yup)
    endif()

    # YUP's iconutil-based helper rejects its generated iconset on current
    # macOS. Keep the compatibility fix project-local until upstream changes.
    if(APPLE)
        include(cmake/YupMacOSIconWorkaround.cmake)
    endif()

    if(APPLE)
        set(SHARDNOISE_CREATE_AU ON)
    else()
        set(SHARDNOISE_CREATE_AU OFF)
    endif()

    yup_audio_plugin(
        TARGET_NAME shardnoise
        TARGET_VERSION ${PROJECT_VERSION}
        TARGET_IDE_GROUP "ShardNoise"
        TARGET_APP_ID "jp.ehl.shardnoise"
        TARGET_APP_NAMESPACE "jp.ehl"
        TARGET_CXX_STANDARD 20
        PLUGIN_ID "jp.ehl.shardnoise"
        PLUGIN_NAME "ShardNoise"
        PLUGIN_VENDOR "ehl_"
        PLUGIN_EMAIL ""
        PLUGIN_VERSION "${PROJECT_VERSION}"
        PLUGIN_DESCRIPTION "MIDI-triggered high-frequency binary noise instrument"
        PLUGIN_URL "https://github.com/EsionHsrahLatigid/ShardNoise"
        PLUGIN_IS_SYNTH ON
        PLUGIN_IS_MONO OFF
        PLUGIN_CREATE_CLAP OFF
        PLUGIN_CREATE_VST3 ON
        PLUGIN_CREATE_AU ${SHARDNOISE_CREATE_AU}
        PLUGIN_AU_SUBTYPE "ShRd"
        PLUGIN_AU_MANUFACTURER "EHL1"
        PLUGIN_AU_SANDBOX_SAFE ON
        PLUGIN_CREATE_AUv3 OFF
        PLUGIN_CREATE_LV2 OFF
        PLUGIN_CREATE_AAX OFF
        PLUGIN_CREATE_STANDALONE ON
        PLUGIN_COPY_AFTER_BUILD OFF
        MODULES
            yup::yup_gui
            yup::yup_audio_processors)

    target_sources(shardnoise_shared INTERFACE
        source/violent/ParameterGridEditor.cpp
        source/violent/ParameterGridEditor.h
        source/violent/ProductState.h
        source/violent/plugins/ShardNoisePlugin.cpp
        source/violent/plugins/ShardNoisePlugin.h)
    target_link_libraries(shardnoise_shared INTERFACE shardnoise_engine)
    target_include_directories(shardnoise_shared INTERFACE
        "${CMAKE_CURRENT_LIST_DIR}/include"
        "${CMAKE_CURRENT_LIST_DIR}/source")

    if(SHARDNOISE_BUILD_TESTS)
        add_executable(shardnoise_plugin_tests
            tests/ShardNoisePluginTests.cpp)
        target_link_libraries(shardnoise_plugin_tests PRIVATE shardnoise_shared sdl::sdl)
        target_compile_features(shardnoise_plugin_tests PRIVATE cxx_std_20)
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
            target_compile_options(shardnoise_plugin_tests PRIVATE -Wall -Wextra -Wpedantic)
        endif()
        enable_test_assertions(shardnoise_plugin_tests)
        add_test(NAME shardnoise_plugin_tests COMMAND shardnoise_plugin_tests)
    endif()

    include(cmake/EhlYupArtifactLayout.cmake)
    ehl_yup_add_artifact_target(
        TARGET ehl_stage_products
        PRODUCT ShardNoise
        SLUG shardnoise
        STANDALONE_TARGET shardnoise_standalone_plugin
        VST3_TARGET shardnoise_vst3_plugin
        AU_TARGET shardnoise_au_plugin)

    if(APPLE AND SHARDNOISE_ADHOC_SIGN)
        find_program(SHARDNOISE_CODESIGN_EXECUTABLE codesign REQUIRED)
        add_custom_command(TARGET shardnoise_vst3_plugin POST_BUILD
            COMMAND "${SHARDNOISE_CODESIGN_EXECUTABLE}" --force --deep --sign -
                    "$<TARGET_BUNDLE_DIR:shardnoise_vst3_plugin>"
            COMMENT "Ad-hoc signing ShardNoise VST3 for local loading"
            VERBATIM)
        add_custom_command(TARGET shardnoise_standalone_plugin POST_BUILD
            COMMAND "${SHARDNOISE_CODESIGN_EXECUTABLE}" --force --deep --sign -
                    "$<TARGET_BUNDLE_DIR:shardnoise_standalone_plugin>"
            COMMENT "Ad-hoc signing ShardNoise standalone app for local launch"
            VERBATIM)
    endif()
endif()
