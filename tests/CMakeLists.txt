add_executable(v2d_tests test_pipeline.cpp)
target_link_libraries(v2d_tests PRIVATE v2d_core)

if(MSVC)
  target_compile_options(v2d_tests PRIVATE /W4 /permissive-)
  target_compile_definitions(v2d_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(v2d_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME v2d_tests COMMAND v2d_tests)

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_FOUND)
  execute_process(
    COMMAND ${Python3_EXECUTABLE} -c "import numpy"
    RESULT_VARIABLE V2D_NUMPY_MISSING
    OUTPUT_QUIET ERROR_QUIET)
  if(V2D_NUMPY_MISSING EQUAL 0)
    add_test(NAME loader_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/../loader/test_loader.py)
    message(STATUS "vid2desmos: loader tests enabled")
  else()
    message(STATUS "vid2desmos: numpy not found, skipping loader tests")
  endif()
endif()
