cmake_minimum_required(VERSION 3.22)

# idf.py sets ESP_PLATFORM. This directory is the SDL simulator, not firmware.
if(ESP_PLATFORM)
    message(FATAL_ERROR
        "This is the desktop simulator project. For the CYD, run idf.py with "
        "-C ${CMAKE_CURRENT_SOURCE_DIR}/firmware/esp32-2432S028R "
        "-B ${CMAKE_CURRENT_SOURCE_DIR}/firmware/esp32-2432S028R/build/idf-v6.0.3 "
        "(or cd to that firmware directory first).")
endif()

project(aliens LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(FetchContent)

set(LV_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h" CACHE STRING "" FORCE)
set(LV_CONF_INCLUDE_SIMPLE ON CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
option(ALIENS_FETCH_LVGL "Download LVGL with CMake if external/lvgl is missing" ON)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/lvgl/CMakeLists.txt")
    add_subdirectory(external/lvgl)
elseif(ALIENS_FETCH_LVGL)
    FetchContent_Declare(
        lvgl
        GIT_REPOSITORY https://github.com/lvgl/lvgl.git
        GIT_TAG v9.3.0
        GIT_SHALLOW TRUE
        GIT_PROGRESS TRUE
    )

    FetchContent_MakeAvailable(lvgl)
else()
    message(FATAL_ERROR "LVGL not found. Clone it to external/lvgl or configure with -DALIENS_FETCH_LVGL=ON.")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)

if(TARGET lvgl::lvgl)
    set(LVGL_TARGET lvgl::lvgl)
elseif(TARGET lvgl)
    set(LVGL_TARGET lvgl)
else()
    message(FATAL_ERROR "LVGL CMake target was not created.")
endif()

set(SENTRY_FONTS
    src/fonts/font_sentry_digits_28.c
    src/fonts/font_sentry_caption_17.c
    src/fonts/font_sentry_critical_14.c
    src/fonts/font_sentry_station_28.c
)
add_library(sentry_ui
    app/sentry_ui.c
    ${SENTRY_FONTS}
)

target_include_directories(sentry_ui PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/app"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(sentry_ui PUBLIC
    ${LVGL_TARGET}
    m
)

add_library(sentry_bell app/sentry_bell.c)
target_include_directories(sentry_bell PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/app")
target_link_libraries(sentry_bell PUBLIC m)

add_executable(aliens_bell sim/bell_preview.c)
target_link_libraries(aliens_bell PRIVATE sentry_ui sentry_bell ${SDL2_LIBRARIES})
target_include_directories(aliens_bell PRIVATE ${SDL2_INCLUDE_DIRS})
target_link_directories(aliens_bell PRIVATE ${SDL2_LIBRARY_DIRS})

add_executable(aliens
    sim/main_sdl.c
)

add_executable(aliens_gauge
    sim/gauge_isolation.c
    app/sentry_ui.c
    ${SENTRY_FONTS}
)

target_compile_definitions(aliens_gauge PRIVATE ALIENS_TEST)

target_include_directories(aliens PRIVATE
    ${SDL2_INCLUDE_DIRS}
)

target_include_directories(aliens_gauge PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/app"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    ${SDL2_INCLUDE_DIRS}
)

target_compile_options(aliens PRIVATE ${SDL2_CFLAGS_OTHER})
target_compile_options(aliens_gauge PRIVATE ${SDL2_CFLAGS_OTHER})

target_link_directories(aliens PRIVATE ${SDL2_LIBRARY_DIRS})
target_link_directories(aliens_gauge PRIVATE ${SDL2_LIBRARY_DIRS})

target_link_libraries(aliens PRIVATE
    sentry_ui
    ${SDL2_LIBRARIES}
)

target_link_libraries(aliens_gauge PRIVATE
    ${LVGL_TARGET}
    ${SDL2_LIBRARIES}
    m
)

include(CTest)
if(BUILD_TESTING)
    add_executable(test_critical sim/test_critical.c)
    target_link_libraries(test_critical PRIVATE sentry_ui ${SDL2_LIBRARIES})
    target_link_directories(test_critical PRIVATE ${SDL2_LIBRARY_DIRS})
    target_include_directories(test_critical PRIVATE ${SDL2_INCLUDE_DIRS})
    add_test(NAME critical_lifecycle COMMAND test_critical)
    add_executable(test_bell sim/test_bell.c)
    target_link_libraries(test_bell PRIVATE sentry_bell)
    add_test(NAME bell_synthesis COMMAND test_bell)
    add_test(NAME bell_preview_clock COMMAND aliens_bell --wav "${CMAKE_CURRENT_BINARY_DIR}/bell-clock.wav")
    add_test(NAME bell_preview_stalled_ui COMMAND aliens_bell --wav "${CMAKE_CURRENT_BINARY_DIR}/bell-stalled.wav" --stall-ui)
    set_tests_properties(bell_preview_clock bell_preview_stalled_ui PROPERTIES FIXTURES_SETUP audio_previews)
    add_test(NAME bell_ui_stall_invariance COMMAND ${CMAKE_COMMAND} -E compare_files
        "${CMAKE_CURRENT_BINARY_DIR}/bell-clock.wav" "${CMAKE_CURRENT_BINARY_DIR}/bell-stalled.wav")
    set_tests_properties(bell_ui_stall_invariance PROPERTIES FIXTURES_REQUIRED audio_previews)
endif()
