// ============================================================================
// test_fileops.cpp — Phase 5 Advanced File Operations Tests
// Tests: MDTM format, recursive directory deletion, rename
// ============================================================================
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef min
#undef max

#include "common/platform.h"
#include <cstdio>
#include <string>
#include <fstream>
#include <regex>

// ─── Mini test framework ─────────────────────────────────────────────────────
static int g_pass = 0, g_fail = 0;
void check(bool cond, const char* name) {
    printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
    cond ? g_pass++ : g_fail++;
    fflush(stdout);
}

// ─── Tests ───────────────────────────────────────────────────────────────────

void test_mdtm() {
    printf("\nTest: MDTM Format (File Modification Time)\n");

    std::string testFile = "build\\test_mdtm.txt";
    ftp::platform::deleteFile(testFile);

    // Create a file
    std::ofstream f(testFile);
    f << "hello";
    f.close();

    std::string mdtm = ftp::platform::getFileModTime(testFile);
    printf("  MDTM Output: %s\n", mdtm.c_str());

    check(!mdtm.empty(), "MDTM output is not empty");
    check(mdtm.size() == 14, "MDTM output is exactly 14 characters");

    // Check format: YYYYMMDDHHMMSS (all digits)
    bool allDigits = true;
    for (char c : mdtm) {
        if (c < '0' || c > '9') allDigits = false;
    }
    check(allDigits, "MDTM output consists only of digits");

    ftp::platform::deleteFile(testFile);
}

void test_recursive_rmdir() {
    printf("\nTest: Recursive Directory Deletion\n");

    std::string rootDir = "build\\test_rmdir_root";
    std::string subDir1 = rootDir + "\\sub1";
    std::string subDir2 = rootDir + "\\sub2";
    
    // Create hierarchy
    ftp::platform::createDir(rootDir);
    ftp::platform::createDir(subDir1);
    ftp::platform::createDir(subDir2);

    // Create files inside
    std::ofstream(subDir1 + "\\file1.txt") << "test";
    std::ofstream(subDir2 + "\\file2.txt") << "test";
    std::ofstream(rootDir + "\\file3.txt") << "test";

    check(ftp::platform::pathExists(rootDir), "Root directory created");

    // Delete recursively
    bool rmOk = ftp::platform::removeDirRecursive(rootDir);
    check(rmOk, "removeDirRecursive returned true");
    check(!ftp::platform::pathExists(rootDir), "Root directory and all contents successfully deleted");
}

void test_rename() {
    printf("\nTest: File Renaming\n");
    
    std::string oldName = "build\\test_rename_old.txt";
    std::string newName = "build\\test_rename_new.txt";
    
    ftp::platform::deleteFile(oldName);
    ftp::platform::deleteFile(newName);
    
    std::ofstream(oldName) << "test";
    check(ftp::platform::pathExists(oldName), "Old file created");
    
    bool renOk = ftp::platform::renameFile(oldName, newName);
    check(renOk, "renameFile returned true");
    check(!ftp::platform::pathExists(oldName), "Old file no longer exists");
    check(ftp::platform::pathExists(newName), "New file exists");
    
    ftp::platform::deleteFile(newName);
}

// ─── Main ─────────────────────────────────────────────────────────────────────
int main() {
    printf("============================================\n");
    printf("  Phase 5 Test Suite: File Operations\n");
    printf("============================================\n");

    test_mdtm();
    test_recursive_rmdir();
    test_rename();

    printf("\n============================================\n");
    printf("  Results: %d passed, %d failed\n", g_pass, g_fail);
    printf("============================================\n");
    return (g_fail > 0) ? 1 : 0;
}
