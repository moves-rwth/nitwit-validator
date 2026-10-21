// Pointer, array and function declarators, nested and abstract.
int *p;
char *const cp;
int *const *volatile pp;
int a2[2][3];
int *ap[4];
int (*pa)[4];
int (*fp)(int, ...);
void (*handlers[8])(int);
int (*(*getter)(void))(char);
int (*select(int which))(double);
unsigned long sizes = sizeof(int (*)[3]) + sizeof(char *(*)(void));
void adjusted(int v[10], const int w[static 2], char m[*], int f(void));
