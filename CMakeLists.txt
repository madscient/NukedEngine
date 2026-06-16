cmake_minimum_required(VERSION 3.15)
project(NukedEngine VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -----------------------------------------------------------------------
# オプション
# -----------------------------------------------------------------------
option(NUKEDENGINE_BUILD_SHARED  "共有ライブラリ (DLL/SO) をビルド" ON)
option(NUKEDENGINE_BUILD_STATIC  "静的ライブラリをビルド"            ON)
option(NUKEDENGINE_BUILD_EXAMPLE "使用例をビルド"                   ON)

# -----------------------------------------------------------------------
# Git サブモジュールの初期化チェック
# cmake 実行時にサブモジュールが未取得なら自動で初期化・更新する。
# -----------------------------------------------------------------------
find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
    # サブモジュールが空かどうかを代表ファイルで確認
    set(_submodule_sentinel "${CMAKE_SOURCE_DIR}/cores/opl3/opl3.h")
    if(NOT EXISTS "${_submodule_sentinel}")
        message(STATUS "Git submodules not initialized. Running git submodule update --init --recursive ...")
        execute_process(
            COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            RESULT_VARIABLE GIT_SUBMOD_RESULT
        )
        if(NOT GIT_SUBMOD_RESULT EQUAL 0)
            message(FATAL_ERROR
                "git submodule update failed (exit code ${GIT_SUBMOD_RESULT}).\n"
                "Please run manually:\n"
                "  git submodule update --init --recursive")
        endif()
        message(STATUS "Submodules initialized.")
    endif()
endif()

# サブモジュールの存在を必須チェック
foreach(_core opl3/opl3.h opl2/opl2.h opn2/ym3438.h opm/opm.h opll/opll.h psg/ympsg.h)
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/cores/${_core}")
        message(FATAL_ERROR
            "Required submodule file not found: cores/${_core}\n"
            "Please run:\n"
            "  git submodule update --init --recursive")
    endif()
endforeach()

# -----------------------------------------------------------------------
# Nuked コアソース (git submodule として cores/ 以下に配置)
# -----------------------------------------------------------------------
set(NUKED_SOURCES
    cores/opl3/opl3.c
    cores/opl2/opl2.c
    cores/opn2/ym3438.c
    cores/opm/opm.c
    cores/opll/opll.c
    cores/psg/ympsg.c
)

set(ENGINE_SOURCES
    src/NukedEngineApi.cpp
    ${NUKED_SOURCES}
)

set(ENGINE_INCLUDES
    include
    cores/opl3
    cores/opl2
    cores/opn2
    cores/opm
    cores/opll
    cores/psg
)

# -----------------------------------------------------------------------
# 共有ライブラリ (DLL)
# -----------------------------------------------------------------------
if(NUKEDENGINE_BUILD_SHARED)
    add_library(NukedEngineApi SHARED ${ENGINE_SOURCES})
    target_include_directories(NukedEngineApi PRIVATE ${ENGINE_INCLUDES})
    target_compile_definitions(NukedEngineApi PRIVATE NUKEDENGINE_EXPORTS)

    if(MSVC)
        target_sources(NukedEngineApi PRIVATE src/NukedEngineApi.def)
        target_compile_options(NukedEngineApi PRIVATE /W3 /utf-8
            $<$<CONFIG:Debug>:/O0 /RTC1>
            $<$<CONFIG:Release>:/O2>
        )
    else()
        target_compile_options(NukedEngineApi PRIVATE -Wall -O2)
    endif()

    if(WIN32)
        target_link_libraries(NukedEngineApi PRIVATE avrt ole32)
    endif()

    set_target_properties(NukedEngineApi PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
        PUBLIC_HEADER "include/NukedEngineApi.h"
    )
    install(TARGETS NukedEngineApi
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        PUBLIC_HEADER DESTINATION include)
endif()

# -----------------------------------------------------------------------
# 静的ライブラリ
# -----------------------------------------------------------------------
if(NUKEDENGINE_BUILD_STATIC)
    add_library(NukedEngineApi_static STATIC ${ENGINE_SOURCES})
    target_include_directories(NukedEngineApi_static PRIVATE ${ENGINE_INCLUDES})
    target_compile_definitions(NukedEngineApi_static PRIVATE NUKEDENGINE_STATIC)
    set_target_properties(NukedEngineApi_static PROPERTIES
        OUTPUT_NAME NukedEngineApi_s    # DLL インポートライブラリ (NukedEngineApi.lib) と衝突しないよう "_s" を付ける
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
        POSITION_INDEPENDENT_CODE ON
    )
    if(MSVC)
        target_compile_options(NukedEngineApi_static PRIVATE /W3 /utf-8
            $<$<CONFIG:Debug>:/O0 /RTC1>
            $<$<CONFIG:Release>:/O2>
        )
    endif()

    install(TARGETS NukedEngineApi_static ARCHIVE DESTINATION lib)
endif()

# -----------------------------------------------------------------------
# 使用例
# -----------------------------------------------------------------------
if(NUKEDENGINE_BUILD_EXAMPLE)
    # nlohmann/json (header-only) を FetchContent で取得
    include(FetchContent)
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG        v3.11.3
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(nlohmann_json)

    add_executable(sample_app example/sample_app.cpp)
    target_include_directories(sample_app PRIVATE include)
    target_link_libraries(sample_app PRIVATE nlohmann_json::nlohmann_json)
    if(NUKEDENGINE_BUILD_SHARED)
        target_link_libraries(sample_app PRIVATE NukedEngineApi)
    elseif(NUKEDENGINE_BUILD_STATIC)
        target_link_libraries(sample_app PRIVATE NukedEngineApi_static)
        target_compile_definitions(sample_app PRIVATE NUKEDENGINE_STATIC)
    endif()
    set_target_properties(sample_app PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
    if(MSVC)
        target_compile_options(sample_app PRIVATE /utf-8)
    endif()
    if(UNIX)
        target_link_libraries(sample_app PRIVATE m)
    endif()

    # patches フォルダをビルド出力先にコピー
    add_custom_command(TARGET sample_app POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/example/patches"
            "$<TARGET_FILE_DIR:sample_app>/patches"
        COMMENT "Copying patches to output directory"
    )
endif()
