set(inc "include")

idf_component_register(INCLUDE_DIRS ${inc}
                       WHOLE_ARCHIVE)

add_prebuilt_library(libmorse "lib/esp32-xtensa-lx7/libmorse_nocrypto.a"
                     PRIV_REQUIRES esp_rom mmpktmem)

target_link_libraries(${COMPONENT_LIB} INTERFACE libmorse)
