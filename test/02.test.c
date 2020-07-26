extern void __VERIFIER_error();

typedef struct A {
	int x;
	int y;	
} A;

typedef struct B {
   A a;
} B;

int main() {	
	B z = {{1,3}};
    if (z.a.x == 1 && z.a.y == 3) {
        __VERIFIER_error();
    } else {
        return 0;
    }
}

