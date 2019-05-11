int calculate_output(int);
extern void __VERIFIER_error(void);
extern int __VERIFIER_nondet_int(void);
extern void exit(int);

// inputs
int inputC = 3;
int inputD = 4;
int inputE = 5;
int inputF = 6;
int inputB = 2;


int a1 = 23;
int a19 = 9;
int a10 = 0;
int a12 = 0;
int a4 = 14;

int calculate_output(int input) {
    __VERIFIER_error();
    return input;
}

int main()
{
    // default output
    int output = -1;

    // main i/o-loop
    while(1)
    {
        // read input
        int input;
        input = __VERIFIER_nondet_int();
        if ((input != 2) && (input != 3) && (input != 4) && (input != 5) && (input != 6)) return -2;

        // operate eca engine
        output = calculate_output(input);
    }
}
