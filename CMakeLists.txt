cmake_minimum_required(VERSION 3.16)
project(uasm CXX)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED OFF)
if(NOT DEFINED CMAKE_CXX_EXTENSIONS)
  set(CMAKE_CXX_EXTENSIONS ON)
endif()

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

include(GNUInstallDirs)
find_package(Threads)
if(NOT WIN32)
  set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
endif()

add_library(uasm SHARED
  src/core/types.cpp
  src/core/extensions.cpp
  src/core/opcode_info.cpp
  src/core/value.cpp
  src/frontend/lexer.cpp
  src/frontend/parser.cpp
  src/linker/linker.cpp
  src/vm/interpreter.cpp
  src/format/serializer.cpp
  src/format/disassemble.cpp
  src/util/glob.cpp
  src/util/generic/glob.cpp
  src/util/posix/glob.cpp
  src/util/windows/glob.cpp
  src/util/posix/jit_memory.cpp
  src/util/windows/jit_memory.cpp
  src/vm/syscall.cpp
  src/vm/posix/syscall.cpp
  src/vm/windows/syscall.cpp
  src/codegen/codegen.cpp
  src/codegen/host_target.cpp
  src/codegen/arm64/encoder.cpp
  src/codegen/macos/writer.cpp
  src/vm/jit.cpp
)
target_include_directories(uasm PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
if(MSVC)
  target_compile_options(uasm PRIVATE /W4)
else()
  target_compile_options(uasm PRIVATE -Wall -Wextra)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_options(uasm PRIVATE -static-libgcc -static-libstdc++)
endif()
if(Threads_FOUND)
  target_link_libraries(uasm PRIVATE Threads::Threads)
endif()
if(WIN32)
  target_link_libraries(uasm PRIVATE ws2_32)
endif()

add_executable(uasm_cli src/cli/main.cpp)
target_link_libraries(uasm_cli PRIVATE uasm)
set_target_properties(uasm_cli PROPERTIES OUTPUT_NAME uasm)
if(MSVC)
  target_compile_options(uasm_cli PRIVATE /W4)
else()
  target_compile_options(uasm_cli PRIVATE -Wall -Wextra)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_options(uasm_cli PRIVATE -static-libgcc -static-libstdc++)
endif()

install(TARGETS uasm
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(TARGETS uasm_cli
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/uasm DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
