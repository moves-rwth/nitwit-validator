extern void __VERIFIER_error();


int main() {
    char str[3] = "abs";
    int x[] = {1,2,3}; // x has type int[3] and holds 1,2,3
    int y[5] = {1,2,3}; // y has type int[5] and holds 1,2,3,0,0
    int z[3] = {0}; // z has type int[3] and holds all zeroes
    int a[4][3] = { // array of 4 arrays of 3 ints each (4x3 matrix)
            { 1 },      // row 0 initialized to {1, 0, 0}
            { 0, 1 },   // row 1 initialized to {0, 1, 0}
            { 0, 0, 1 },  // row 2 initialized to {0, 0, 1}
    };              // row 3 initialized to {0, 0, 0}


    __VERIFIER_error();
}

