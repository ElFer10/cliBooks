cmake_minimum_required(VERSION 3.20)
project(clibooks VERSION 0.1.0 LANGUAGES C CXX)

# C++23 when the compiler supports it, otherwise C++20 (minimum).
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED OFF)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------- curses
# Prefer a wide-character ncurses via pkg-config (Linux, Homebrew); fall back to
# FindCurses (macOS system ncurses is wide-capable). Windows: PDCurses (set CURSES_*).
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
  pkg_check_modules(NCURSESW IMPORTED_TARGET ncursesw)
endif()
if(NCURSESW_FOUND)
  set(CLIBOOKS_CURSES PkgConfig::NCURSESW)
else()
  set(CURSES_NEED_NCURSES TRUE)
  find_package(Curses REQUIRED)
  add_library(clibooks_curses INTERFACE)
  target_include_directories(clibooks_curses INTERFACE ${CURSES_INCLUDE_DIRS})
  target_link_libraries(clibooks_curses INTERFACE ${CURSES_LIBRARIES})
  set(CLIBOOKS_CURSES clibooks_curses)
endif()

# ---------------------------------------------------------------- vendored libs
add_library(clibooks_thirdparty STATIC
  third_party/miniz/miniz.c
  third_party/pugixml/pugixml.cpp)
target_include_directories(clibooks_thirdparty SYSTEM PUBLIC
  third_party/miniz third_party/pugixml third_party/tomlplusplus third_party/stb)
set_target_properties(clibooks_thirdparty PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------- core library
add_library(clibooks_core STATIC
  src/util/utf8.cpp
  src/util/strings.cpp
  src/util/platform.cpp
  src/epub/zip_archive.cpp
  src/epub/css.cpp
  src/epub/html_parser.cpp
  src/epub/book.cpp
  src/layout/hyphen.cpp
  src/layout/layout.cpp
  src/image/image.cpp
  src/app/i18n.cpp
  src/app/config.cpp
  src/app/keymap.cpp
  src/app/state.cpp)
target_include_directories(clibooks_core PUBLIC src)
target_link_libraries(clibooks_core PUBLIC clibooks_thirdparty)
target_compile_definitions(clibooks_core PUBLIC TOML_EXCEPTIONS=0)

add_executable(clibooks
  src/ui/terminal.cpp
  src/ui/reader.cpp
  src/ui/app.cpp
  src/main.cpp)
target_compile_definitions(clibooks PRIVATE NCURSES_WIDECHAR=1 _XOPEN_SOURCE_EXTENDED=1 CLIBOOKS_VERSION="${PROJECT_VERSION}")
target_link_libraries(clibooks PRIVATE clibooks_core ${CLIBOOKS_CURSES})

if(MSVC)
  target_compile_options(clibooks_core PRIVATE /W4 /utf-8)
  target_compile_options(clibooks PRIVATE /W4 /utf-8)
else()
  target_compile_options(clibooks_core PRIVATE -Wall -Wextra -Wno-unused-parameter)
  target_compile_options(clibooks PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# ---------------------------------------------------------------- tests
option(CLIBOOKS_BUILD_TESTS "Build unit tests" ON)
if(CLIBOOKS_BUILD_TESTS)
  enable_testing()
  add_executable(clibooks_tests tests/tests.cpp)
  target_link_libraries(clibooks_tests PRIVATE clibooks_core)
  target_compile_definitions(clibooks_tests PRIVATE CLIBOOKS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  add_test(NAME clibooks_tests COMMAND clibooks_tests)
endif()

install(TARGETS clibooks RUNTIME DESTINATION bin)
