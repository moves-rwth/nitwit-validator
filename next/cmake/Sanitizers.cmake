# Applies NITWIT_SANITIZE to everything, including third-party code, so that containers are instrumented consistently.
if(NITWIT_SANITIZE)
    add_compile_options(-fsanitize=${NITWIT_SANITIZE} -fno-sanitize-recover=all -fno-omit-frame-pointer)
    add_link_options(-fsanitize=${NITWIT_SANITIZE})
endif()
