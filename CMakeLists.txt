cmake_minimum_required(VERSION 3.31)

if(APPLE)
    project(VoidFurnace VERSION 0.1.0 LANGUAGES C CXX OBJC OBJCXX)
else()
    project(VoidFurnace VERSION 0.1.0 LANGUAGES C CXX)
endif()

option(VOIDFURNACE_BUILD_PLUGIN "Build the YUP Standalone/VST3/AU targets" ON)
option(VOIDFURNACE_BUILD_TESTS "Build deterministic DSP regression tests" ON)
option(VOIDFURNACE_ADHOC_SIGN "Ad-hoc sign local macOS plugin and app bundles after building" ON)
set(VOIDFURNACE_CREATE_AU OFF)
if(APPLE)
    set(VOIDFURNACE_CREATE_AU ON)
endif()
set(VOIDFURNACE_YUP_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../yup" CACHE PATH
    "Path to a local YUP checkout; fetched from upstream when unavailable")

add_library(voidfurnace_engine STATIC
    source/VoidFurnaceEngine.cpp)
target_include_directories(voidfurnace_engine PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/include")
target_compile_features(voidfurnace_engine PUBLIC cxx_std_20)
set_target_properties(voidfurnace_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(voidfurnace_engine PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(VOIDFURNACE_BUILD_TESTS)
    include(CTest)

    function(enable_test_assertions target_name)
        if(MSVC)
            target_compile_options(${target_name} PRIVATE /UNDEBUG)
        elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
            target_compile_options(${target_name} PRIVATE -UNDEBUG)
        endif()
    endfunction()

    add_executable(voidfurnace_engine_tests
        tests/VoidFurnaceEngineTests.cpp)
    target_link_libraries(voidfurnace_engine_tests PRIVATE voidfurnace_engine)
    target_compile_features(voidfurnace_engine_tests PRIVATE cxx_std_20)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(voidfurnace_engine_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    enable_test_assertions(voidfurnace_engine_tests)
    add_test(NAME voidfurnace_engine_tests COMMAND voidfurnace_engine_tests)
endif()

if(VOIDFURNACE_BUILD_PLUGIN)
    set(YUP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(YUP_BUILD_PLUGINS OFF CACHE BOOL "" FORCE)
    set(YUP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(YUP_ENABLE_PLUGINVAL OFF CACHE BOOL "" FORCE)
    set(YUP_ENABLE_VST3_VALIDATOR OFF CACHE BOOL "" FORCE)
    set(YUP_ENABLE_CLAP_VALIDATOR OFF CACHE BOOL "" FORCE)
    set(YUP_ENABLE_AUVAL_VALIDATOR OFF CACHE BOOL "" FORCE)

    if(EXISTS "${VOIDFURNACE_YUP_SOURCE_DIR}/CMakeLists.txt")
        add_subdirectory("${VOIDFURNACE_YUP_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/yup")
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

    yup_audio_plugin(
        TARGET_NAME voidfurnace
        TARGET_VERSION ${PROJECT_VERSION}
        TARGET_IDE_GROUP "VoidFurnace"
        TARGET_APP_ID "jp.ehl.voidfurnace"
        TARGET_APP_NAMESPACE "jp.ehl"
        TARGET_CXX_STANDARD 20
        PLUGIN_ID "jp.ehl.voidfurnace"
        PLUGIN_NAME "VoidFurnace"
        PLUGIN_VENDOR "ehl_"
        PLUGIN_EMAIL ""
        PLUGIN_VERSION "${PROJECT_VERSION}"
        PLUGIN_DESCRIPTION "Hadamard feedback-delay-network reverb with dark velvet diffusion"
        PLUGIN_URL "https://github.com/EsionHsrahLatigid/VoidFurnace"
        PLUGIN_IS_SYNTH OFF
        PLUGIN_IS_MONO OFF
        PLUGIN_CREATE_CLAP OFF
        PLUGIN_CREATE_VST3 ON
        PLUGIN_CREATE_AU ${VOIDFURNACE_CREATE_AU}
        PLUGIN_AU_SUBTYPE "VdFn"
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

    target_sources(voidfurnace_shared INTERFACE
        source/ParameterGridEditor.cpp
        source/ParameterGridEditor.h
        source/ProductState.h
        source/VoidFurnacePlugin.cpp
        source/VoidFurnacePlugin.h)
    target_link_libraries(voidfurnace_shared INTERFACE voidfurnace_engine)
    target_include_directories(voidfurnace_shared INTERFACE
        "${CMAKE_CURRENT_LIST_DIR}/include"
        "${CMAKE_CURRENT_LIST_DIR}/source")

    if(VOIDFURNACE_BUILD_TESTS)
        add_executable(voidfurnace_plugin_bridge_tests
            tests/VoidFurnacePluginBridgeTests.cpp
            source/VoidFurnacePlugin.cpp)
        target_link_libraries(voidfurnace_plugin_bridge_tests PRIVATE voidfurnace_engine yup::yup_audio_processors)
        target_include_directories(voidfurnace_plugin_bridge_tests PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/include"
            "${CMAKE_CURRENT_LIST_DIR}/source")
        target_compile_definitions(voidfurnace_plugin_bridge_tests PRIVATE VOIDFURNACE_HEADLESS_TEST=1)
        target_compile_features(voidfurnace_plugin_bridge_tests PRIVATE cxx_std_20)
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
            target_compile_options(voidfurnace_plugin_bridge_tests PRIVATE -Wall -Wextra -Wpedantic)
        endif()
        enable_test_assertions(voidfurnace_plugin_bridge_tests)
        add_test(NAME voidfurnace_plugin_bridge_tests COMMAND voidfurnace_plugin_bridge_tests)

        add_executable(voidfurnace_standalone_bridge_tests
            tests/VoidFurnaceStandaloneBridgeTests.cpp
            source/VoidFurnacePlugin.cpp)
        target_link_libraries(voidfurnace_standalone_bridge_tests PRIVATE voidfurnace_engine yup::yup_audio_processors)
        target_include_directories(voidfurnace_standalone_bridge_tests PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/include"
            "${CMAKE_CURRENT_LIST_DIR}/source")
        target_compile_definitions(voidfurnace_standalone_bridge_tests PRIVATE
            VOIDFURNACE_HEADLESS_TEST=1
            YUP_AUDIO_PLUGIN_ENABLE_STANDALONE=1)
        target_compile_features(voidfurnace_standalone_bridge_tests PRIVATE cxx_std_20)
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
            target_compile_options(voidfurnace_standalone_bridge_tests PRIVATE -Wall -Wextra -Wpedantic)
        endif()
        enable_test_assertions(voidfurnace_standalone_bridge_tests)
        add_test(NAME voidfurnace_standalone_bridge_tests COMMAND voidfurnace_standalone_bridge_tests)
    endif()

    set(voidfurnace_release_bundle_targets
        voidfurnace_standalone_plugin
        voidfurnace_vst3_plugin)
    if(APPLE)
        list(APPEND voidfurnace_release_bundle_targets voidfurnace_au_plugin)
    endif()

    add_custom_target(voidfurnace_release_bundles
        DEPENDS ${voidfurnace_release_bundle_targets})

    include(cmake/EhlYupArtifactLayout.cmake)
    ehl_yup_add_artifact_target(
        TARGET ehl_stage_products
        PRODUCT VoidFurnace
        SLUG voidfurnace
        STANDALONE_TARGET voidfurnace_standalone_plugin
        VST3_TARGET voidfurnace_vst3_plugin
        AU_TARGET voidfurnace_au_plugin)

    if(APPLE)
        set_target_properties(voidfurnace_standalone_plugin PROPERTIES
            MACOSX_BUNDLE_GUI_IDENTIFIER "jp.ehl.voidfurnace"
            MACOSX_BUNDLE_BUNDLE_NAME "VoidFurnace"
            XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "jp.ehl.voidfurnace")
    endif()

    if(APPLE AND VOIDFURNACE_ADHOC_SIGN)
        find_program(VOIDFURNACE_CODESIGN_EXECUTABLE codesign REQUIRED)
        add_custom_command(TARGET voidfurnace_vst3_plugin POST_BUILD
            COMMAND "${VOIDFURNACE_CODESIGN_EXECUTABLE}" --force --deep --sign -
                    "$<TARGET_BUNDLE_DIR:voidfurnace_vst3_plugin>"
            COMMENT "Ad-hoc signing VoidFurnace VST3 for local loading"
            VERBATIM)
        add_custom_command(TARGET voidfurnace_standalone_plugin POST_BUILD
            COMMAND "${VOIDFURNACE_CODESIGN_EXECUTABLE}" --force --deep --sign -
                    "$<TARGET_BUNDLE_DIR:voidfurnace_standalone_plugin>"
            COMMENT "Ad-hoc signing VoidFurnace standalone app for local launch"
            VERBATIM)
    endif()
endif()
