if(BOARD STREQUAL "NRF5340" OR BOARD STREQUAL "nrf7002dk_nrf5340_cpuapp")
    # set(BOARD_SDK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/${BOARD})
    set(BOARD_SDK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/Zephyr)


    set(BOARD_SRCS
        ${BOARD_SDK_DIR}/Zephyr.c
    )
    set(BOARD_INCS_DIR
        ${BOARD_SDK_DIR}
        ${BOARD_SDK_DIR}/include
    )
    set(BOARD_LIBS_DIR
        ${BOARD_SDK_DIR}/lib
    )
    set(BOARD_LIBS_SHARED
        nrf7002dk_nrf5340_cpuapp.a
    )
    set(BOARD_LIBS_STATIC
        nrf7002dk_nrf5340_cpuapp.a
    )
endif()
