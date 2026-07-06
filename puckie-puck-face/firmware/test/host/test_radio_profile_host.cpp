#include <cstdio>
#include <cstring>

#include "radio_profile.h"

static int failures = 0;

#define CHECK(cond)                                                 \
    do {                                                            \
        if (!(cond)) {                                              \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                             \
        }                                                           \
    } while (0)

int main() {
    CHECK(strcmp(profile::bandNameForFrequency(14074000), "20m") == 0);
    CHECK(strcmp(profile::bandNameForFrequency(5330500), "60m") == 0);
    CHECK(strcmp(profile::bandNameForFrequency(145500000), "GEN") == 0);

    if (failures == 0) {
        printf("radio profile host tests: ALL PASS\n");
        return 0;
    }
    printf("radio profile host tests: %d FAILURES\n", failures);
    return 1;
}
