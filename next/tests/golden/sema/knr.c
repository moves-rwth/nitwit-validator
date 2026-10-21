// Pre-C99 code: K&R definitions, implicit int and implicit function declarations.
add(a, b)
    short a;
    double b;
{
    return a + b;
}

main() {
    int x = add(1, 2.5f);
    return helper(x) + add(x);
}

helper(v) {
    return v;
}
