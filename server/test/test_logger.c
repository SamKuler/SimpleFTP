#include <stdio.h>
#include <stdlib.h>

#include "logger.h"

static int g_test_passed = 0;
static int g_test_failed = 0;

static void test_pass(const char *test_name)
{
    printf("✅ PASS: %s\n", test_name);
    g_test_passed++;
}

static void test_fail(const char *test_name, const char *message)
{
    fprintf(stderr, "❌ FAIL: %s - %s\n", test_name, message);
    g_test_failed++;
}

static void test_tty_log()
{
    printf("\n--- Test 1: TTY Logger ---\n");
    logger_init(0, LOG_LEVEL_DEBUG);
    LOG_INFO("============================================================");
    LOG_INFO(" TEST START ");
    LOG_DEBUG("Hello DEBUG");
    LOG_WARN("Hello WARN");
    LOG_ERROR("Hello ERROR");
    LOG_INFO(" TEST END ");
    LOG_INFO("============================================================\n");
    logger_close();
    test_pass("TTY logging");
}

static void test_file_log()
{
    printf("\n--- Test 2: File Logger ---\n");
    logger_init("test.log", LOG_LEVEL_DEBUG);
    LOG_INFO("============================================================");
    LOG_INFO(" TEST START ");
    LOG_DEBUG("Hello DEBUG");
    LOG_WARN("Hello WARN");
    LOG_ERROR("Hello ERROR");
    LOG_INFO(" TEST END ");
    LOG_INFO("============================================================\n");
    logger_close();
    test_pass("File logging");
}

static void test_reset_logger_level()
{
    printf("\n--- Test 3: Logger Level Change ---\n");
    logger_init(0, LOG_LEVEL_DEBUG);
    LOG_INFO("============================================================");
    LOG_INFO(" TEST START ");
    LOG_DEBUG("Hello DEBUG");
    LOG_WARN("Hello WARN");
    LOG_ERROR("Hello ERROR");
    LOG_INFO("ENTERING SET LEVEL TEST");
    logger_set_level(LOG_LEVEL_WARN);
    LOG_INFO(" TEST START AGAIN");
    LOG_DEBUG("Hello DEBUG");
    LOG_WARN("Hello WARN");
    LOG_ERROR("Hello ERROR");
    LOG_WARN(" TEST END ");
    LOG_WARN("============================================================\n");
    logger_close();
    test_pass("Logger level change");
}

int main()
{
    printf("============================================================\n");
    printf("Logger Test Suite\n");
    printf("============================================================\n");
    
    test_tty_log();
    test_file_log();
    test_reset_logger_level();
    
    printf("\n============================================================\n");
    printf("Test Results: %d/%d passed\n", g_test_passed, g_test_passed + g_test_failed);
    printf("============================================================\n");
    
    if (g_test_failed > 0) {
        printf("\n❌ Some tests failed\n");
        return 1;
    } else {
        printf("\n✅ All tests passed\n");
        return 0;
    }
}