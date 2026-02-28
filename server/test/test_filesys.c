#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

#include "filesys.h"

static int g_test_passed = 0;
static int g_test_failed = 0;
static char g_test_dir[PATH_MAX];

static void test_pass(const char *test_name)
{
    printf("✅ PASS: %s\n", test_name);
    g_test_passed++;
}

static void test_fail(const char *test_name, const char *message)
{
    fprintf(stderr, "❌ FAIL: %s - %s\n", test_name, message);
    g_test_failed++;
    exit(1);
}

static void test_write_file()
{
    printf("\n--- Test 1: Write File ---\n");

    char path[PATH_MAX];
    const char *fname = "file1.txt";
    snprintf(path, PATH_MAX, "%s/%s", g_test_dir, fname);

    const char *text = "Hello World";
    long long written = fs_write_file_all(path, (void *)text, (long long)strlen(text));
    if (written != (long long)strlen(text))
        test_fail("Write file", "fs_write_file_all failed");
    test_pass("Write file");
}

static void test_get_file_size()
{
    printf("\n--- Test 2: Get File Size ---\n");

    char path[PATH_MAX];
    snprintf(path, PATH_MAX, "%s/file1.txt", g_test_dir);

    const char *text = "Hello World";
    long long fsize = fs_get_file_size(path);
    if (fsize != (long long)strlen(text))
        test_fail("Get file size", "size mismatch");
    test_pass("Get file size");
}

static void test_read_file()
{
    printf("\n--- Test 3: Read File ---\n");

    char path[PATH_MAX];
    snprintf(path, PATH_MAX, "%s/file1.txt", g_test_dir);

    const char *text = "Hello World";
    char buf[128];
    long long readn = fs_read_file_all(path, buf, sizeof(buf));
    long long fsize = fs_get_file_size(path);

    if (readn != fsize)
        test_fail("Read file", "size mismatch");
    buf[readn] = '\0';
    if (strcmp(buf, text) != 0)
        test_fail("Read file", "content mismatch");
    test_pass("Read file");
}

static void test_write_file_chunk()
{
    printf("\n--- Test 4: Write File Chunk ---\n");

    char path[PATH_MAX];
    snprintf(path, PATH_MAX, "%s/file1.txt", g_test_dir);

    const char *text = "Hello World";
    const char *append = "_CHUNK";
    long long fsize = fs_get_file_size(path);

    long long chunk_written = fs_write_file_chunk(path, append, fsize, (long long)strlen(append));
    if (chunk_written != (long long)strlen(append))
        test_fail("Write file chunk", "write failed");

    long long newsize = fs_get_file_size(path);
    if (newsize != fsize + (long long)strlen(append))
        test_fail("Write file chunk", "size mismatch after chunk write");

    char buf2[256];
    long long re = fs_read_file_all(path, buf2, sizeof(buf2));
    if (re != newsize)
        test_fail("Write file chunk", "read after chunk failed");
    buf2[re] = '\0';

    /* verify content contains both parts */
    if (strstr(buf2, text) == NULL || strstr(buf2, append) == NULL)
        test_fail("Write file chunk", "combined content mismatch");
    test_pass("Write file chunk");
}

static void test_list_directory()
{
    printf("\n--- Test 5: List Directory ---\n");

    const char *fname = "file1.txt";
    fs_file_info_t list[16];
    int listed = fs_list_directory(g_test_dir, list, 16);
    if (listed <= 0)
        test_fail("List directory", "returned no entries");

    int found = 0;
    for (int i = 0; i < listed; ++i)
    {
        if (strcmp(list[i].name, fname) == 0)
        {
            found = 1;
            break;
        }
    }
    if (!found)
        test_fail("List directory", "did not find expected file");
    test_pass("List directory");
}

static void test_get_directory_size()
{
    printf("\n--- Test 6: Get Directory Size ---\n");

    char path[PATH_MAX];
    snprintf(path, PATH_MAX, "%s/file1.txt", g_test_dir);
    long long filesize = fs_get_file_size(path);

    long long dsize = fs_get_directory_size(g_test_dir);
    if (dsize < filesize)
        test_fail("Get directory size", "size too small");
    test_pass("Get directory size");
}

static void test_delete_file()
{
    printf("\n--- Test 7: Delete File ---\n");

    char path[PATH_MAX];
    snprintf(path, PATH_MAX, "%s/file1.txt", g_test_dir);

    if (fs_delete_file(path) != 0)
        test_fail("Delete file", "fs_delete_file failed");

    if (fs_path_exists(path))
        test_fail("Delete file", "file still exists after delete");
    test_pass("Delete file");
}

static void test_create_and_delete_directory()
{
    printf("\n--- Test 8: Create and Delete Directory ---\n");

    char subdir[PATH_MAX];
    snprintf(subdir, PATH_MAX, "%s/subdir", g_test_dir);
    if (fs_create_directory(subdir) != 0)
        test_fail("Create directory", "fs_create_directory failed");

    char subfile[PATH_MAX];
    snprintf(subfile, PATH_MAX, "%s/%s", subdir, "inner.txt");
    const char *inner = "inner";
    if (fs_write_file_all(subfile, (void *)inner, (long long)strlen(inner)) != (long long)strlen(inner))
        test_fail("Create directory", "write inner file failed");

    if (fs_delete_directory(g_test_dir, 1) != 0)
        test_fail("Delete directory", "recursive delete failed");

    if (fs_path_exists(g_test_dir))
        test_fail("Delete directory", "directory still exists after recursive delete");
    test_pass("Create and delete directory");
}

static void test_extract_filename()
{
    printf("\n--- Test 9: Extract Filename ---\n");

#ifdef _WIN32
    const char *test_paths[] = {
        "C:\\path\\to\\file.txt",
        "C:\\path\\to\\directory\\",
        "file_only.txt",
        "C:\\",
        "",
        NULL};
    const char *expected_filenames[] = {
        "file.txt",
        "",
        "file_only.txt",
        "",
        "",
    };
#else
    const char *test_paths[] = {
        "/path/to/file.txt",
        "/path/to/directory/",
        "file_only.txt",
        "/",
        "",
        NULL};
    const char *expected_filenames[] = {
        "file.txt",
        "",
        "file_only.txt",
        "",
        "",
    };
#endif

    for (int i = 0; test_paths[i] != NULL; ++i)
    {
        const char *extracted = fs_extract_filename(test_paths[i]);
        if (strcmp(extracted, expected_filenames[i]) != 0)
        {
            test_fail("Extract filename", "mismatch");
        }
    }
    test_pass("Extract filename");
}

static void test_get_parent_directory()
{
    printf("\n--- Test: Get Parent Directory ---\n");
    struct
    {
        const char *input;
        const char *expected_parent;
        int expected_result;
    } tests[] = {
        {"/", "/", 0},  // Root directory - parent is itself
        {"//", "/", 0}, // Multiple slashes - normalize to /
        {"/home", "/", 0},
        {"/home/user", "/home", 0},
        {"/home/user/documents/file.txt", "/home/user/documents", 0},
        {"/home/user/", "/home", 0},
        {"/home/user///", "/home", 0},
        {"file.txt", NULL, -1},
        {"dir/file.txt", "dir", 0},
        {"", NULL, -1},
        {NULL, NULL, 0} // Sentinel
    };

    for (int i = 0; tests[i].input != NULL; ++i)
    {
        char parent[PATH_MAX];
        int result = fs_get_parent_directory(tests[i].input, parent, sizeof(parent));

        if (result != tests[i].expected_result)
        {
            char msg[256];
            snprintf(msg, sizeof(msg), "fs_get_parent_directory('%s') result: expected %d, got %d",
                     tests[i].input, tests[i].expected_result, result);
            test_fail("Parent directory - result check", msg);
        }

        if (result == 0)
        {
            if (strcmp(parent, tests[i].expected_parent) != 0)
            {
                char msg[256];
                snprintf(msg, sizeof(msg), "fs_get_parent_directory('%s') parent: expected '%s', got '%s'",
                         tests[i].input, tests[i].expected_parent, parent);
                test_fail("Parent directory - path check", msg);
            }
        }
    }

    /* verify insufficient buffer detection */
    char tiny_parent[8];
    if (fs_get_parent_directory("/this/is/too/long", tiny_parent, sizeof(tiny_parent)) != -1)
    {
        test_fail("Parent directory - buffer check", "should fail when buffer is too small");
    }

#ifdef _WIN32
    char win_parent[PATH_MAX];
    if (fs_get_parent_directory("C:\\dir\\file.txt", win_parent, sizeof(win_parent)) != 0 ||
        strcmp(win_parent, "C:\\dir") != 0)
    {
        test_fail("Parent directory - Windows path", "parent mismatch");
    }

    if (fs_get_parent_directory("C:\\", win_parent, sizeof(win_parent)) != 0 ||
        strcmp(win_parent, "C:\\") != 0)
    {
        test_fail("Parent directory - Windows root", "drive root mismatch");
    }
#endif
    test_pass("Get parent directory");
}

int main()
{
    printf("============================================================\n");
    printf("Filesystem Test Suite\n");
    printf("============================================================\n");

#ifdef _WIN32
    char template[] = "filesys_test_XXXXXX";
    if (_mktemp(template) == NULL)
        test_fail("Setup", "_mktemp failed");
    if (!CreateDirectoryA(template, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
        test_fail("Setup", "CreateDirectory failed");
    char *dir = template;
#else
    char template[] = "/tmp/filesys_test_XXXXXX";
    char *dir = mkdtemp(template);
    if (!dir)
        test_fail("Setup", "mkdtemp failed");
#endif

    // Store test directory globally
    strncpy(g_test_dir, dir, PATH_MAX - 1);
    g_test_dir[PATH_MAX - 1] = '\0';

    // Run all tests
    test_write_file();
    test_get_file_size();
    test_read_file();
    test_write_file_chunk();
    test_list_directory();
    test_get_directory_size();
    test_delete_file();
    test_create_and_delete_directory();
    test_extract_filename();
    test_get_parent_directory();

    printf("\n============================================================\n");
    printf("Test Results: %d/%d passed\n", g_test_passed, g_test_passed + g_test_failed);
    printf("============================================================\n");

    if (g_test_failed > 0)
    {
        printf("\n❌ Some tests failed\n");
        return 1;
    }
    else
    {
        printf("\n✅ All tests passed\n");
        return 0;
    }
}
