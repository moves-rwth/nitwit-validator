//extern void __VERIFIER_error();
#include <stdio.h>

struct S {
    const char * ch;
    const short sh;
};

int main()
{
    char const * c = "23";
    const int a = 90;
    const char * s = "23";
    double f = 2.0;
    const double * d = &f;

    struct S st;

    st.ch = "asdf";
    st.sh = 2;
    return 0;
}

