set(src
    "src/rf-test.c"
    "src/mmhal_uart.c")

set(inc
    "src")

idf_component_register(INCLUDE_DIRS ${inc}
                       SRCS ${src}
                       PRIV_REQUIRES driver morselib mm_shims mmutils)
