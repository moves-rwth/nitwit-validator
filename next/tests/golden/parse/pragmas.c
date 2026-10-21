// #pragma pack and #pragma weak; other pragmas are ignored.
#pragma GCC diagnostic ignored "-Wall"
#pragma pack(push, 1)
struct packed1 { char c; int i; };
#pragma pack(push)
#pragma pack(2)
struct packed2 { char c; int i; };
#pragma pack(pop)
struct still1 { char c; int i; };
#pragma pack(pop)
struct natural { char c; int i; };

#pragma weak optional
void optional(void);
void caller(void) {
#pragma unknown pragma inside a function
    if (optional)
        optional();
}
