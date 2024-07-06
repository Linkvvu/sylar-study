#include "../debug.h"

void ExpectTrue() {
    SYLAR_ASSERT(true);
}

void ExpectFalse() {
    SYLAR_ASSERT(false);
}

int main() {
    ExpectTrue();
    ExpectFalse();
}