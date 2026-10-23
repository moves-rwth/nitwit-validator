nitwit_add_golden_tests(sema nitwit-dump sema --sema)
# The same for i386, for the types and layouts that depend on the data model.
nitwit_add_golden_tests(sema/ilp32 nitwit-dump sema --sema -32)
