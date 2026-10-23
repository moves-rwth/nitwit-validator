// Flexible array members, zero-length arrays, empty records, anonymous members and arrays completed by initializers.

struct f1 { int n; char data[]; };
struct f2 { char c; int data[]; };
struct f3 { char c; double data[]; };
struct f4 { int n; char data[0]; };
struct f5 { char c; long long data[0]; char d; };
struct f6 {};
struct f7 { struct f6 e; char c; struct f6 f; };
struct f8 { char c; struct { int a; char b; }; union { short s; double d; }; char tail; };
struct f9 { char c; struct { char d; struct { long long x; char y; }; }; };
union f10 { struct { char a, b; }; int i; };

int completed_array[] = {1, 2, 3, [10] = 4};
char completed_string[] = "hello";
struct { int a; short b[]; } unnamed_flexible;
int matrix[][3] = {{1, 2}, {3}, [5][1] = 1};
char strings[][4] = {"ab", "cde"};
