cmake_minimum_required(VERSION 3.20)
project(ArchiveCleaner LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/utf-8)
endif()

# ===== Core 库（纯 Win32 + STL，零框架依赖）=====
add_library(ac_core STATIC
    src/core/PathUtils.cpp
    src/core/Config.cpp
    src/core/ISearchEngine.cpp
    src/core/WalkEngine.cpp
    src/core/ProcessRunner.cpp
    src/core/FdEngine.cpp
    src/core/EverythingEngine.cpp
    src/core/MftEngine.cpp
    src/core/ProcessChecker.cpp
    src/core/Deleter.cpp
)
target_include_directories(ac_core PUBLIC src)

if(WIN32)
    target_link_libraries(ac_core PUBLIC
        shlwapi advapi32 rstrtmgr shell32 ole32
    )
endif()

# ===== 接入 EUI-NEO（已迁移到项目内）=====
set(EUI_SRC "${CMAKE_CURRENT_SOURCE_DIR}/third_party/eui-neo-src/EUI-NEO-0.5.6")
add_subdirectory("${EUI_SRC}" "${CMAKE_BINARY_DIR}/eui_neo_build")

# ===== Domain 层（依赖 core + EUI-NEO 的 app::async）=====
add_library(ac_domain STATIC
    src/domain/AppModel.cpp
)
target_link_libraries(ac_domain PUBLIC ac_core eui::neo)
target_include_directories(ac_domain PUBLIC src)

# ===== 主程序（EUI-NEO GUI）=====
set(EUI_APP_MAIN "${EUI_SRC}/core/app/glfw_app_main.cpp")

add_executable(ArchiveCleaner
    src/main.cpp
    ${EUI_APP_MAIN}
)
target_link_libraries(ArchiveCleaner PRIVATE ac_domain)
eui_neo_configure_app(ArchiveCleaner)

# 确保 assets 拷到 exe 目录（运行时窗口图标需要）
add_custom_command(TARGET ArchiveCleaner POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ArchiveCleaner>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${EUI_SRC}/assets" "$<TARGET_FILE_DIR:ArchiveCleaner>/assets"
)

# 显式加入图标 rc 资源（eui_neo_configure_app 的 target_sources 因 scope 问题未生效）
# EUI 的 CMakeLists 生成了 eui_app_icon.rc（指向 EUI 源码的 assets/icon.ico，已替换为 AC 图标）
set(EUI_ICON_RC "${CMAKE_BINARY_DIR}/eui_neo_build/eui_app_icon.rc")
if(EXISTS "${EUI_ICON_RC}")
    target_sources(ArchiveCleaner PRIVATE "${EUI_ICON_RC}")
endif()

# ===== 测试程序（core 独立验证，不依赖 EUI-NEO）=====
add_executable(ac_test src/test_main.cpp)
target_link_libraries(ac_test PRIVATE ac_core)
if(WIN32)
    set_target_properties(ac_test PROPERTIES WIN32_EXECUTABLE OFF)
endif()

add_executable(ac_del_test src/del_test.cpp)
target_link_libraries(ac_del_test PRIVATE ac_core)
if(WIN32)
    set_target_properties(ac_del_test PROPERTIES WIN32_EXECUTABLE OFF)
endif()
