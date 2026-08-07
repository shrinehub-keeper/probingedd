idf_component_register(SRCS "sms.c" "system.c" "vdp.c" "sn76496.c" "render.c" "z80.c" "z80_ops.c"
                        INCLUDE_DIRS "."
                        REQUIRES "")

target_compile_options(${COMPONENT_LIB} PRIVATE -DLSB_FIRST=0 -Wno-pointer-sign)
