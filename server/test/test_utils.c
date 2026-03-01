/**
 * @file test_utils.c
 * @brief Unit tests for utility functions (especially thread safety)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "utils.h"

static int g_test_passed = 0;
static int g_test_failed = 0;

static void test_pass(const char *test_name)
{
    printf("PASS: %s\n", test_name);
    g_test_passed++;
}

static void test_fail(const char *test_name, const char *message)
{
    fprintf(stderr, "FAIL: %s - %s\n", test_name, message);
    g_test_failed++;
}

// ---------------------------------------------------------------------------
// get_timestamp tests
// ---------------------------------------------------------------------------

static void test_get_timestamp_basic(void)
{
    char buffer[64];
    get_timestamp(buffer, sizeof(buffer));

    // Validate format: YYYY-MM-DD HH:MM:SS (19 chars)
    if (strlen(buffer) != 19)
    {
        char msg[128];
        snprintf(msg, sizeof(msg), "Expected 19 chars, got %zu: '%s'", strlen(buffer), buffer);
        test_fail("get_timestamp_basic", msg);
        return;
    }

    // Check dashes and colons at expected positions
    if (buffer[4] != '-' || buffer[7] != '-' || buffer[10] != ' ' ||
        buffer[13] != ':' || buffer[16] != ':')
    {
        char msg[128];
        snprintf(msg, sizeof(msg), "Unexpected format: '%s'", buffer);
        test_fail("get_timestamp_basic", msg);
        return;
    }

    test_pass("get_timestamp_basic");
}

// Thread function for concurrent timestamp test
static void *timestamp_thread_func(void *arg)
{
    int *errors = (int *)arg;
    for (int i = 0; i < 1000; i++)
    {
        char buffer[64];
        get_timestamp(buffer, sizeof(buffer));
        if (strlen(buffer) != 19)
        {
            (*errors)++;
            break;
        }
    }
    return NULL;
}

static void test_get_timestamp_thread_safety(void)
{
    const int num_threads = 8;
    pthread_t threads[8];
    int errors[8] = {0};

    for (int i = 0; i < num_threads; i++)
    {
        pthread_create(&threads[i], NULL, timestamp_thread_func, &errors[i]);
    }

    int total_errors = 0;
    for (int i = 0; i < num_threads; i++)
    {
        pthread_join(threads[i], NULL);
        total_errors += errors[i];
    }

    if (total_errors > 0)
    {
        char msg[128];
        snprintf(msg, sizeof(msg), "%d threads reported errors", total_errors);
        test_fail("get_timestamp_thread_safety", msg);
    }
    else
    {
        test_pass("get_timestamp_thread_safety");
    }
}

// ---------------------------------------------------------------------------
// trim_whitespace tests
// ---------------------------------------------------------------------------

static void test_trim_whitespace(void)
{
    char buf1[] = "  hello world  ";
    trim_whitespace(buf1);
    if (strcmp(buf1, "hello world") != 0)
    {
        test_fail("trim_whitespace_basic", buf1);
        return;
    }

    char buf2[] = "nochange";
    trim_whitespace(buf2);
    if (strcmp(buf2, "nochange") != 0)
    {
        test_fail("trim_whitespace_noop", buf2);
        return;
    }

    char buf3[] = "   ";
    trim_whitespace(buf3);
    if (strcmp(buf3, "") != 0)
    {
        test_fail("trim_whitespace_all_spaces", buf3);
        return;
    }

    // NULL should not crash
    trim_whitespace(NULL);

    test_pass("trim_whitespace");
}

// ---------------------------------------------------------------------------
// to_uppercase tests
// ---------------------------------------------------------------------------

static void test_to_uppercase(void)
{
    char buf[] = "Hello World 123";
    to_uppercase(buf);
    if (strcmp(buf, "HELLO WORLD 123") != 0)
    {
        test_fail("to_uppercase", buf);
        return;
    }

    // NULL should not crash
    to_uppercase(NULL);

    test_pass("to_uppercase");
}

// ---------------------------------------------------------------------------
// crlf_to_lf / lf_to_crlf tests
// ---------------------------------------------------------------------------

static void test_crlf_conversions(void)
{
    // CRLF → LF
    const char input1[] = "hello\r\nworld\r\n";
    char output1[64];
    long long len1 = crlf_to_lf(input1, (long long)strlen(input1), output1, sizeof(output1));
    if (len1 != 12 || memcmp(output1, "hello\nworld\n", 12) != 0)
    {
        test_fail("crlf_to_lf", "conversion mismatch");
        return;
    }

    // LF → CRLF
    const char input2[] = "hello\nworld\n";
    char output2[64];
    long long len2 = lf_to_crlf(input2, (long long)strlen(input2), output2, sizeof(output2));
    if (len2 != 14 || memcmp(output2, "hello\r\nworld\r\n", 14) != 0)
    {
        test_fail("lf_to_crlf", "conversion mismatch");
        return;
    }

    // Error cases
    if (crlf_to_lf(NULL, 5, output1, sizeof(output1)) != -1)
    {
        test_fail("crlf_to_lf_null", "expected -1 for NULL input");
        return;
    }

    test_pass("crlf_conversions");
}

// ---------------------------------------------------------------------------
// sleep_ms basic test
// ---------------------------------------------------------------------------

static void test_sleep_ms(void)
{
    // Just verify it doesn't crash; sleep 10ms
    sleep_ms(10);
    test_pass("sleep_ms");
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(void)
{
    printf("============================================================\n");
    printf("Utils Test Suite\n");
    printf("============================================================\n");

    test_get_timestamp_basic();
    test_get_timestamp_thread_safety();
    test_trim_whitespace();
    test_to_uppercase();
    test_crlf_conversions();
    test_sleep_ms();

    printf("\n============================================================\n");
    printf("Test Results: %d/%d passed\n", g_test_passed, g_test_passed + g_test_failed);
    printf("============================================================\n");

    if (g_test_failed > 0)
    {
        printf("\nSome tests failed\n");
        return 1;
    }
    else
    {
        printf("\nAll tests passed\n");
        return 0;
    }
}
