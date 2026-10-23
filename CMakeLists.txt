cmake_minimum_required(VERSION 3.20)
project(std_t98_receiver VERSION 0.2.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(SPEAR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/spear)
find_path(FFTW3F_INCLUDE_DIR fftw3.h HINTS "${CMAKE_CURRENT_SOURCE_DIR}/build/fftw" REQUIRED)
if(MINGW AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/build/fftw/libfftw3f-3.dll")
  set(FFTW3F_LIBRARY "${CMAKE_CURRENT_SOURCE_DIR}/build/fftw/libfftw3f-3.dll")
else()
  find_library(FFTW3F_LIBRARY NAMES fftw3f libfftw3f-3 HINTS "${CMAKE_CURRENT_SOURCE_DIR}/build/fftw" REQUIRED)
endif()
add_library(t98_dsp STATIC
  ${SPEAR}/apps/std_t98/receiver.cpp
  ${SPEAR}/apps/std_t98/protocol/protocol.cpp
  ${SPEAR}/apps/std_t98/dsp/rrc.cpp
  ${SPEAR}/apps/std_t98/dsp/symbol_sync.cpp
  ${SPEAR}/apps/std_t98/dsp/sync_correlator.cpp
  ${SPEAR}/dsp/src/fir.cpp
  ${SPEAR}/dsp/src/fftw_planner.cpp)
target_include_directories(t98_dsp PRIVATE ${FFTW3F_INCLUDE_DIR})
target_link_libraries(t98_dsp PUBLIC ${FFTW3F_LIBRARY})
target_include_directories(t98_dsp PUBLIC
  src src/include ${SPEAR}/apps/std_t98 ${SPEAR}/dsp/include)
if(MSVC)
  target_compile_options(t98_dsp PUBLIC /utf-8 /permissive-)
endif()

add_library(t98_io STATIC src/input.cpp src/output.cpp src/dashboard.cpp)
find_package(Threads REQUIRED)
target_link_libraries(t98_io PUBLIC t98_dsp Threads::Threads ${CMAKE_DL_LIBS})
target_include_directories(t98_io PUBLIC src)
add_executable(std-t98 src/main.cpp)
target_link_libraries(std-t98 PRIVATE t98_io)
if(WIN32)
  target_link_libraries(std-t98 PRIVATE shell32)
endif()
if(MINGW)
  # Statically link the C++ runtime; FFTW and librtlsdr remain external DLLs.
  target_link_options(std-t98 PRIVATE -static -static-libgcc -static-libstdc++)
endif()
if(WIN32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/build/fftw/libfftw3f-3.dll")
  add_custom_command(TARGET std-t98 POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/build/fftw/libfftw3f-3.dll" "$<TARGET_FILE_DIR:std-t98>")
  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/build/fftw/libfftw3f-3.dll" DESTINATION bin)
endif()
install(TARGETS std-t98 RUNTIME DESTINATION bin)

include(CTest)
if(BUILD_TESTING)
  add_executable(t98-tests tests/native/test_main.cpp ${SPEAR}/dsp/src/channelizer.cpp)
  target_include_directories(t98-tests PRIVATE ${FFTW3F_INCLUDE_DIR})
  target_include_directories(t98-tests PRIVATE ${SPEAR}/apps/std_t98/tests)
  target_link_libraries(t98-tests PRIVATE t98_io)
  if(WIN32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/build/fftw/libfftw3f-3.dll")
    add_custom_command(TARGET t98-tests POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/build/fftw/libfftw3f-3.dll" "$<TARGET_FILE_DIR:t98-tests>")
  endif()
  add_test(NAME native COMMAND t98-tests)
  add_test(NAME cli_help COMMAND std-t98 --help)
endif()
