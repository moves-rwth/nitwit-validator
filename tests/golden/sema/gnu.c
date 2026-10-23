// GNU extensions: statement expressions, typeof, __auto_type, __builtin_choose_expr, _Generic and
// __builtin_types_compatible_p.
int x;
typeof(x + 1L) widened;
typeof(int[3]) three;

int gnu(int y) {
    __auto_type copy = y * 2.0;
    int max = ({
        int a = x, b = y;
        a > b ? a : b;
    });
    long chosen = __builtin_choose_expr(sizeof(long) == 8, 1L, 1.0);
    int selected = _Generic(y, long: 1, int: 2, default: 3);
    return max + chosen + selected + __builtin_types_compatible_p(typeof(x), int) + (int)copy;
}
