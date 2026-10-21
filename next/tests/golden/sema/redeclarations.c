// Redeclarations get the composite type (C17 6.2.7); tentative definitions of arrays get one element.
extern int numbers[];
int numbers[4];
int counts[];
int sum();
int sum(int a, int b) {
    return a + b;
}
int sum(int, int);

int use(void) {
    extern int numbers[];
    return sum(numbers[0], sizeof numbers);
}
