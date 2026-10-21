// float and double arithmetic in IEEE 754 binary32/binary64 (SSE, also with -m32), printf of floating-point values,
// infinities, NaN, signed zero and subnormals.
int printf(const char *, ...);
double sqrt(double);
int __VERIFIER_nondet_int(void);

int main(void) {
    double a = 0.1, b = 0.2;
    printf("%.17g %d\n", a + b, a + b == 0.3);
    float fa = 0.1f, fb = 0.2f;
    printf("%.9g %d\n", fa + fb, fa + fb == 0.3f);
    printf("%.17g %.9g\n", 1.0 / 3, 1.0f / 3);
    // float operands are not promoted to double: the arithmetic happens in float.
    float big = 16777216.0f;
    printf("%.1f %.1f\n", big + 1.0f, big + 1.0);

    printf("%f %e %g %a\n", 1234.5678, 1234.5678, 1234.5678, 1234.5678);
    printf("%.3f %.0f %.0f %.0f %10.4f|%-10.2e|\n", 2.0 / 3, 0.5, 1.5, 2.5, -3.14159, 31415.9);
    printf("%g %g %g %g\n", 100000.0, 1000000.0, 0.0001, 0.00001);

    double huge = 1e308;
    double inf = huge * 10;
    double nan = inf - inf;
    printf("%f %f %d %d %d\n", inf, -inf, nan != nan, nan == nan, inf > huge);
    printf("%d %d\n", nan < 1.0, nan > 1.0);
    double negativeZero = -0.0;
    printf("%f %g %d %f\n", negativeZero, negativeZero, negativeZero == 0.0, 1.0 / inf);
    double tiny = 2.2250738585072014e-308 / 4;
    printf("%a %g\n", tiny, tiny);
    float tinyFloat = 1.17549435e-38f / 8;
    printf("%a\n", tinyFloat);

    printf("%.17g %.17g\n", sqrt(2.0), sqrt(1e-300));

    int k = __VERIFIER_nondet_int();
    double x = k;
    printf("%f %f %f\n", x / 7, x * 0.1, (x - 0.5) * (x + 0.5));
    double accumulator = 0;
    for (int i = 0; i < 10; ++i) {
        accumulator += 0.1;
    }
    printf("%.17g %d\n", accumulator, accumulator == 1.0);
    float sum = 0;
    for (int i = 1; i <= k * 1000; ++i) {
        sum += 1.0f / i;
    }
    printf("%.9g\n", sum);
    printf("%d %d %d\n", 0.1f == 0.1, (float)0.5 == 0.5, 1e16 + 1 == 1e16);
    return 0;
}
