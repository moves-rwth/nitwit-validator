extern void __VERIFIER_error();

#include <assert.h>

int main() {
    double blah = 99.99;

    assert(blah < 100);

    __VERIFIER_error();

    assert(blah > 100); // fail
}

