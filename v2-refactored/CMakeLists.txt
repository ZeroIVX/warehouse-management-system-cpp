cmake_minimum_required(VERSION 3.20)
project(WarehouseSystem VERSION 2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (NOT WIN32)
    message(FATAL_ERROR "本项目依赖 Win32 控制台 API（SetConsoleOutputCP / GetModuleFileNameW），仅支持 Windows。")
endif ()

if (MSVC)
    # /utf-8：源码与执行字符集统一 UTF-8，避免中文乱码
    # NOMINMAX + UNICODE：避免 min/max 宏冲突，并让 Win32 接口走宽字符版本
    add_compile_options(/utf-8 /W3 /MP /DUNICODE /D_UNICODE /DNOMINMAX)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else ()
    add_compile_options(-Wall -Wextra -O2)
    add_compile_definitions(UNICODE _UNICODE NOMINMAX)
endif ()

# ---------------------------------------------------------------------------
#  核心库：业务 + 持久化 + 控制台抽象（三个可执行目标共用）
# ---------------------------------------------------------------------------
add_library(wh_core STATIC
    src/console.cpp
    src/repository.cpp
    src/service.cpp
)
target_include_directories(wh_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# ---------------------------------------------------------------------------
#  可执行目标
# ---------------------------------------------------------------------------
add_executable(warehouse
    src/main.cpp
    src/console_ui.cpp
)
target_link_libraries(warehouse PRIVATE wh_core)

add_executable(wh_tests tests/test_main.cpp)
target_link_libraries(wh_tests PRIVATE wh_core)

add_executable(wh_bench bench/bench.cpp)
target_link_libraries(wh_bench PRIVATE wh_core)

# ---------------------------------------------------------------------------
#  测试
# ---------------------------------------------------------------------------
enable_testing()
add_test(NAME unit_tests COMMAND wh_tests)

# 依赖关系：测试与基准都只依赖头文件与核心库，不需要主程序产物
add_dependencies(wh_tests wh_core)
add_dependencies(wh_bench wh_core)
