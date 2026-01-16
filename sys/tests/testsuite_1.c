#include "testsuite_1.h"
#include "error.h"
#include "console.h"

// Add args, structs, includes, defines
void run_testsuite_1() {
    int retval = -EINVAL;
    char * test_output;

    retval = test_1();
    test_output = (retval == 0) ? "test1 passed!" : "test1 failed!"; 
    kprintf("%s\n", test_output);
}

// Make whatever tests you want.
int test_1() {
    return 0;
}