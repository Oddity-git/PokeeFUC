# Opt-in source-based coverage; keep production and sanitizer builds separate.
option(PSPRECOMP_COVERAGE "Instrument native tests for LLVM source coverage" OFF)
if(PSPRECOMP_COVERAGE)
    if(CMAKE_CROSSCOMPILING OR NOT CMAKE_SYSTEM_NAME MATCHES "^(Linux|Darwin)$"
       OR NOT CMAKE_CXX_COMPILER_ID MATCHES "^(Clang|AppleClang)$")
        message(FATAL_ERROR "PSPRECOMP_COVERAGE requires native Linux/macOS Clang")
    endif()
    if(PSPRECOMP_SANITIZERS)
        message(FATAL_ERROR "Use separate build directories for coverage and sanitizers")
    endif()
    # The flags are Clang's. A C compiler of another family (third-party C
    # sources, such as the KTX2 decoder's zstd) is left uninstrumented.
    add_compile_options("$<$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang>:-fprofile-instr-generate;-fcoverage-mapping;-O0;-g>"
                        "$<$<COMPILE_LANG_AND_ID:C,Clang,AppleClang>:-fprofile-instr-generate;-fcoverage-mapping;-O0;-g>")
    add_link_options(-fprofile-instr-generate)
endif()
