extern void __VERIFIER_error();

typedef struct A {
	int x;	
} A;

typedef struct B {
   A a;
   int f;
} B;

int main() {	
	B z = {{1},2};
    if (z.f != 2) {
        return 0;
    } else {
        __VERIFIER_error();
    }
}

