void reach_error(void) {}

int main(void) {
    if (sizeof(long) == 8) {
        reach_error();
    }
    return 0;
}
