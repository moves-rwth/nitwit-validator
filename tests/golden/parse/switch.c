// Switch statements: cases in nested blocks, case ranges, fall-through, labels at the end of a block.
int classify(int c) {
    int result = 0;
    switch (c) {
    case 0:
    case 1:
        result = 1;
        break;
    case 'a' ... 'z': {
        case 150:
            result = 2;
    }
        __attribute__((fallthrough));
    default:
        switch (result) {
        case 2:
            result = 3;
        }
        break;
    case 200:
    }
    return result;
}
