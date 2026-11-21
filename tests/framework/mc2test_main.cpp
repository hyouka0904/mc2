#include "mc2test.h"

int main(int argc, char** argv)
{
    const char* filter = argc > 1 ? argv[1] : NULL;
    int ran = 0;
    int failedTests = 0;

    for (size_t i = 0; i < mc2test::registry().size(); ++i) {
        const mc2test::TestCase& t = mc2test::registry()[i];
        if (filter && !strstr(t.name, filter))
            continue;

        const int before = mc2test::failures();
        printf("[ RUN  ] %s\n", t.name);
        fflush(stdout);
        t.fn();
        const bool ok = mc2test::failures() == before;
        printf("[ %s ] %s\n", ok ? " OK " : "FAIL", t.name);
        ++ran;
        if (!ok)
            ++failedTests;
    }

    printf("\n%d test(s) run, %d failed\n", ran, failedTests);
    if (ran == 0) {
        fprintf(stderr, "no tests matched\n");
        return 1;
    }
    return failedTests == 0 ? 0 : 1;
}
