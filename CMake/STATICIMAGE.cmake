if(BOARD STREQUAL "STATICIMAGE")
    set(BOARD_SDK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/${BOARD})
 
    set(BOARD_SRCS
        ${BOARD_SDK_DIR}/STATICIMAGEPort.c
    )
    set(BOARD_INCS_DIR
        ${BOARD_SDK_DIR}
    )
    set(BOARD_LIBS_DIR
    )
    set(BOARD_LIBS_SHARED
    )
    set(BOARD_LIBS_STATIC
    )
endif()
