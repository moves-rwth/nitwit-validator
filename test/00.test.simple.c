extern void __VERIFIER_error();

typedef struct A {
	int x, y;	
} A;

typedef struct B {
   A a;
   int f;
} B;

int main() {	
	A z = {1,2};
    if (z.x != 1 && z.y != 2) {
        return 0;
    } else {
        __VERIFIER_error();
    }
}

