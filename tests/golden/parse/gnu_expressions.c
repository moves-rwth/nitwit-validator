// GNU expressions: statement expressions, typeof, __auto_type, ?: without middle operand, builtins.
struct s { int a; struct { int b[4]; } inner; };
int x;
_Complex double z;
__builtin_va_list list;

void gnu(void) {
    int y = ({ int t = x; t * 2; });
    typeof(x + 1) sum = y;
    __typeof__(int *) pointer = &x;
    __auto_type copy = y;
    y = x ?: 7;
    double re = __real__ z + __imag__ z;
    long long ll = __extension__ 1LL;
    y = __builtin_offsetof(struct s, inner.b[2]);
    y = __builtin_types_compatible_p(int, long);
    y = __builtin_choose_expr(1, x, 2);
    y = __builtin_va_arg(list, int);
    y = __builtin_expect(y, 0);
    y = _Generic(x, int: 1, default: 0);
    const char *function = __func__;
    function = __PRETTY_FUNCTION__;
}
