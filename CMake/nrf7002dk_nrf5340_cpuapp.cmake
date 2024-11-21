if(BOARD STREQUAL "NRF5340" OR BOARD STREQUAL "nrf7002dk_nrf5340_cpuapp")
    # set(BOARD_SDK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/${BOARD})
    set(BOARD_SDK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/${BOARD})

    find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
    zephyr_include_directories(${BOARD_SDK_DIR})


    set(BOARD_SRCS
        ${BOARD_SDK_DIR}/${BOARD}Port.c
    )
    set(BOARD_INCS_DIR
        ${BOARD_SDK_DIR}
        ${BOARD_SDK_DIR}/include
    )
    set(BOARD_LIBS_DIR
        ${BOARD_SDK_DIR}/lib
    )
    # set(BOARD_LIBS_SHARED
    #     nrf7002dk_nrf5340_cpuapp.a
    # )
    set(BOARD_LIBS_STATIC
        nrf7002dk_nrf5340_cpuapp.a
    )
endif()
