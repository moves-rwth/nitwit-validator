#include "interpreter.hpp"

void AdjustBitField(struct ParseState* Parser, struct Value *Val) {
    if (Val->BitField > 0){
        unsigned bits = Val->BitField;
        long long mask = (1 << bits) - 1;
        switch (Val->Typ->Base) {
            case BaseType::TypeLongLong:
                Val->Val->LongLongInteger %= 1 << bits;
                if ((1 << (bits-1)) & Val->Val->LongLongInteger)
                    Val->Val->LongLongInteger = -(((mask & (~Val->Val->LongLongInteger + 1)) % (1 << bits)));
                break;
            case BaseType::TypeUnsignedLongLong:
                Val->Val->UnsignedLongLongInteger %= 1 << bits; break;
            default:
                ProgramFail(Parser, "other than integral types can't be bit fields");
        }
    }
}

