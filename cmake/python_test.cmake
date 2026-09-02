include(CTest)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(
      NAME python_test
      COMMAND
          "${CMAKE_COMMAND}" -E env
          "PYTHONPATH=${PROJECT_SOURCE_DIR}/python"
          "${Python3_EXECUTABLE}" -m unittest_parallel
          -s python/tests -t python -p test_*.py --level=module -v
      WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
      CONFIGURATIONS Debug
  )
