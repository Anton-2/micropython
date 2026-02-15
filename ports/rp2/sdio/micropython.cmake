# Create an INTERFACE library for our CPP module.
add_library(usermod_ae_sdio INTERFACE)

# Add our source files to the library.
target_sources(usermod_ae_sdio INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/ae_sdio.c
    ${CMAKE_CURRENT_LIST_DIR}/machine_sdcard.c
    ${CMAKE_CURRENT_LIST_DIR}/sdio_resources.c
    ${CMAKE_CURRENT_LIST_DIR}/sdio_voice.c
    ${CMAKE_CURRENT_LIST_DIR}/src/sdio_rp2350.cpp
    ${CMAKE_CURRENT_LIST_DIR}/machine_voice.c
)

# Add the current directory as an include directory.
target_include_directories(usermod_ae_sdio INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/include
)

# Link our INTERFACE library to the usermod target.
target_link_libraries(usermod INTERFACE usermod_ae_sdio)
