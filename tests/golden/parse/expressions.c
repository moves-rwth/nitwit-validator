// Operators with their precedence and associativity.
int a, b, c, *p;
struct s { int m; struct s *next; } v;

void operators(void) {
    a = b + c * 2 - a / 3 % 4;
    a = b << 2 >> 1;
    a = b < c == c >= a != 0;
    a = b & c ^ a | 1;
    a = b && c || !a;
    a += b -= c *= 2;
    a = b ? c : a ? 1 : 2;
    a = (b, c);
    a = -b + ~c + !p + *p + +a;
    p = &a;
    a = ++b + c++ - --b - c--;
    a = v.m + v.next->next->m + p[1] + 2[p];
    a = sizeof a + sizeof(a) + sizeof(int);
}
