extern void __VERIFIER_error() __attribute__ ((__noreturn__));

int main() {
  unsigned int allOne = -1;

  int castToInt = allOne;
  long castToLong = allOne;
  long castToLong2 = castToInt;
  unsigned long castToULong = allOne;

  if (castToInt == -1 && castToLong == 4294967295UL &&
      castToLong2 == -1 && castToULong == 4294967295UL) {
    goto ERROR;
  }

  return (0);
  ERROR: __VERIFIER_error();
  return (-1);
}

