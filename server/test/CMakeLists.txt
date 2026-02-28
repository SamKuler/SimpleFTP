# ============================================================================
# C Unit Tests
# ============================================================================

# LoggerTest
add_executable(test_logger test_logger.c)
target_link_libraries(test_logger ftpserver)
add_test(NAME LoggerTest COMMAND test_logger)
set_tests_properties(LoggerTest PROPERTIES
                     LABELS "unit;c"
                     TIMEOUT 30)

# FilesystemTest
add_executable(test_filesys test_filesys.c)
target_link_libraries(test_filesys ftpserver)
add_test(NAME FilesysTest COMMAND test_filesys)
set_tests_properties(FilesysTest PROPERTIES
                     LABELS "unit;c"
                     TIMEOUT 30)

# ============================================================================
# Python Integration Tests
# ============================================================================

find_package(Python3 REQUIRED)

# Basic Smoke Test
add_test(NAME FTPBasicTest
         COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_basic.py
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(FTPBasicTest PROPERTIES
                     ENVIRONMENT "PYTHONPATH=${CMAKE_SOURCE_DIR}"
                     TIMEOUT 30
                     LABELS "smoke;quick;python;integration")

# RNFR/RNTO/DELE Commands Test
add_test(NAME FTPRenameDeleteTest
         COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_rnfr_rnto_dele.py
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(FTPRenameDeleteTest PROPERTIES
                     ENVIRONMENT "PYTHONPATH=${CMAKE_SOURCE_DIR}"
                     TIMEOUT 60
                     LABELS "rename;delete;python;integration")

# ABOR Specific Test
add_test(NAME FTPAborTest
         COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_abor.py
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(FTPAborTest PROPERTIES
                     ENVIRONMENT "PYTHONPATH=${CMAKE_SOURCE_DIR}"
                     TIMEOUT 60
                     LABELS "abor;python;integration")

# REST/Resume Test
add_test(NAME FTPRestTest
         COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_rest.py
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(FTPRestTest PROPERTIES
                     ENVIRONMENT "PYTHONPATH=${CMAKE_SOURCE_DIR}"
                     TIMEOUT 60
                     LABELS "rest;resume;python;integration")

# Comprehensive Test
add_test(NAME FTPComprehensiveTest
         COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_comprehensive.py
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(FTPComprehensiveTest PROPERTIES
                     ENVIRONMENT "PYTHONPATH=${CMAKE_SOURCE_DIR}"
                     TIMEOUT 180
                     LABELS "comprehensive;python;integration")

# ============================================================================
# Test Organization
# ============================================================================

# Quick tests - for rapid development feedback
set_tests_properties(LoggerTest FilesysTest FTPBasicTest PROPERTIES
                     LABELS "quick")

# Full test suite
set_tests_properties(FTPRenameDeleteTest FTPAborTest FTPRestTest FTPComprehensiveTest PROPERTIES
                     LABELS "full")
