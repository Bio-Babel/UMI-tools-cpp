# examples/

Step 12_examples: one runnable program per tutorial, reproducing the original
analysis through the C++ port. For a compiled target this is the user-facing
deliverable — a notebook only makes sense if the optional bindings are kept.

Every `*.cpp` here is picked up automatically as an `example_<name>` target, so
`cmake --build` proves each one still compiles and links.
