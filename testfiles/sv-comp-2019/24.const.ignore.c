//extern void __VERIFIER_error();
#include <stdio.h>
int comp(char const   * const  * a, char const   * const  * b) {
    return 0;
}

struct S {
    const char * ch;
    const short sh;
    long long s;
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
    int o = -1;
    long long l = (long long) o;
    int c = l;
    printf("%d, %lli, %d\n", o, l, c);
    return 0;
}

