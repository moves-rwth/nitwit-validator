// C11: _Static_assert, _Alignas, _Alignof, _Atomic, _Generic, _Noreturn, _Thread_local, _Bool, _Complex.
_Static_assert(sizeof(int) == 4, "int has 32 bits");
_Alignas(16) int aligned;
_Alignas(double) char storage[8];
_Atomic int counter;
_Atomic(unsigned long) atomic_long;
_Thread_local static int tls;
_Bool flag;
float _Complex fc;
_Noreturn void quit(int status);

int kind(void) {
    _Static_assert(_Alignof(char) == 1, "");
    return _Generic(flag, _Bool: 1, int: 2, default: 3);
}
