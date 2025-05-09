 # Create an INTERFACE library for our C module.
 5 add_library(usermod_amoled INTERFACE)
 6
 7 # Add our source files to the lib
 8 target_sources(usermod_amoled INTERFACE
 9     ${CMAKE_CURRENT_LIST_DIR}/amoled.c
10     ${CMAKE_CURRENT_LIST_DIR}/amoled_qspi_bus.c
11     ${CMAKE_CURRENT_LIST_DIR}/mpfile/mpfile.c
12     ${CMAKE_CURRENT_LIST_DIR}/jpg/tjpgd565.c
13     )
14
15 # Add the current directory as an include directory.
16 target_include_directories(usermod_amoled INTERFACE
17     ${IDF_PATH}/components/esp_lcd/include/
18     ${CMAKE_CURRENT_LIST_DIR}
19     )
20
21 # Link our INTERFACE library to the usermod target.
22 target_link_libraries(usermod INTERFACE usermod_amoled)
