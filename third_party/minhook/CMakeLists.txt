add_library(ff_minhook STATIC
    src/buffer.c
    src/hook.c
    src/trampoline.c
    src/hde/hde64.c)
target_include_directories(ff_minhook PUBLIC include PRIVATE src src/hde)
if(MSVC)
    target_compile_options(ff_minhook PRIVATE /W0 /WX-)
endif()
