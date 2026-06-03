# set(src_dirs LED RGB_LED)

# set(include_dirs LED RGB_LED)

set(src_dirs LED UART)

set(include_dirs LED UART)

set(requires driver)

idf_component_register(SRC_DIRS "${src_dirs}"
                       INCLUDE_DIRS "${include_dirs}"
                       REQUIRES ${requires})

component_compile_options(-ffast-math -O3 -Wno-error=format=-Wno-format)

