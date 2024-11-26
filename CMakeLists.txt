cmake_minimum_required(VERSION 3.12)
project(embedded-media VERSION 1.0.0 LANGUAGES C CXX)

# Link with top level CMakeLists.txt

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
zephyr_include_directories(.)

set(CMAKE_CROSSCOMPILING "TRUE")
set(CMAKE_SYSTEM_PROCESSOR "armv8-m")
set(CMAKE_EXE_LINKER_FLAGS_INIT "--specs=nosys.specs") #picolibc?
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_VERSION "0.0.0")

# Define the target CPU and architecture
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mcpu=cortex-m33 -march=armv8-m.main+dsp")

# Enable thumb mode explicitly
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mthumb")


set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(USE_MUCLIBC "Use muclibc" OFF)

# Board Specific Parts Start
if(NOT BOARD OR BOARD STREQUAL "")
    message(STATUS "No board was selected. Use FILE by default.")
    set(BOARD "FILE")
elseif(BOARD STREQUAL "FILE")
    message(STATUS "Selected board FILE")
elseif(BOARD STREQUAL "V4L2")
    message(STATUS "Selected board V4L2")
elseif(BOARD STREQUAL "T31")
    message(STATUS "Selected board T31")
elseif(BOARD STREQUAL "FH8626V100")
    message(STATUS "Selected board FH8626V100")
elseif(BOARD STREQUAL "STATICIMAGE")
    # including root repo src/image/include
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../../../../src/image/include)
    message(STATUS "Selected board STATICIMAGE")
elseif(BOARD STREQUAL "ANIMATION")
    message(STATUS "Selected board ANIMATION")
elseif(BOARD STREQUAL "LIVESTREAM")
    message(STATUS "Selected board LIVESTREAM")
else()
    message(FATAL_ERROR "${BOARD} is not implemented yet.")
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/CMake/${BOARD}.cmake)
# Board Specific Parts End

option(BUILD_WEBRTC_SAMPLES "Build webrtc samples" OFF)
option(BUILD_KVS_SAMPLES "Build KVS Producer samples" OFF)
option(BUILD_SAVE_FRAME_SAMPLES "Build save frame samples" OFF)

set(INCS_DIR ${CMAKE_CURRENT_LIST_DIR}/include/)
set(INCS
    ${INCS_DIR}/com/amazonaws/kinesis/video/capability/AudioCapability.h
    ${INCS_DIR}/com/amazonaws/kinesis/video/capability/VideoCapability.h
    ${INCS_DIR}/com/amazonaws/kinesis/video/capturer/VideoCapturer.h
    ${INCS_DIR}/com/amazonaws/kinesis/video/capturer/AudioCapturer.h
    ${INCS_DIR}/com/amazonaws/kinesis/video/player/AudioPlayer.h
    ${ROOT_BUILD_DIR}/zephyr/include/generated/autoconf.h
    ${ZEPHYR_DIR}/include
)

# TODO add def'n if guard for video track
set(SRCS 
    ${CMAKE_CURRENT_LIST_DIR}/source/${BOARD}/${BOARD}VideoCapturer.c
)

if(ENABLE_AUDIO_TRACK)
    set(SRCS
    ${CMAKE_CURRENT_LIST_DIR}/source/${BOARD}/${BOARD}AudioCapturer.c
    ${CMAKE_CURRENT_LIST_DIR}/source/${BOARD}/${BOARD}AudioPlayer.c
    )
endif()

if(NOT BOARD_DESTINATION_PLATFORM OR BOARD_DESTINATION_PLATFORM STREQUAL "")
    set(BOARD_DESTINATION_PLATFORM OFF)
endif()


add_library(embedded-media-static STATIC ${SRCS} ${BOARD_SRCS})
if(BOARD_BUILD_DEPENDS AND NOT (BOARD_BUILD_DEPENDS STREQUAL ""))
    add_dependencies(embedded-media-static ${BOARD_BUILD_DEPENDS})
endif()
target_include_directories(embedded-media-static PRIVATE ${ROOT_BUILD_DIR}/zephyr/include/generated)
target_include_directories(embedded-media-static PRIVATE ${BOARD_INCS_DIR})
target_include_directories(embedded-media-static PUBLIC ${INCS_DIR})
message(STATUS "ROOT_BUILD_DIR is ${ROOT_BUILD_DIR}")
# target_include_directories(embedded-media-static PUBLIC ${ROOT_BUILD_DIR}/zephyr/include/generated)
target_link_directories(embedded-media-static PUBLIC ${BOARD_LIBS_DIR})
target_link_libraries(embedded-media-static PRIVATE ${BOARD_LIBS_STATIC})
set_target_properties(embedded-media-static PROPERTIES PUBLIC_HEADER "${INCS}")
set_target_properties(embedded-media-static PROPERTIES OUTPUT_NAME embedded-media)

target_link_libraries(embedded-media-static PRIVATE
  zephyr_interface
  zephyr_kernel
  zephyr_libc
  zephyr_minimal
  zephyr_posix
  zephyr_syscall
  zephyr_types
)


# add_library(embedded-media-shared SHARED ${SRCS} ${BOARD_SRCS})

# if(BOARD_BUILD_DEPENDS AND NOT(BOARD_BUILD_DEPENDS STREQUAL ""))
#     add_dependencies(embedded-media-shared ${BOARD_BUILD_DEPENDS})
# endif()

# target_include_directories(embedded-media-shared PRIVATE ${BOARD_INCS_DIR})
# target_include_directories(embedded-media-shared PUBLIC ${INCS_DIR})
# target_link_directories(embedded-media-shared PUBLIC ${BOARD_LIBS_DIR})
# target_link_libraries(embedded-media-shared PRIVATE ${BOARD_LIBS_SHARED})
# set_target_properties(embedded-media-shared PROPERTIES PUBLIC_HEADER "${INCS}")
# set_target_properties(embedded-media-shared PROPERTIES OUTPUT_NAME embedded-media)

if(BOARD STREQUAL "QCS610" OR BOARD STREQUAL "GSTREAMER")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0)
    pkg_check_modules(GSTAPP REQUIRED gstreamer-app-1.0)
    target_include_directories(embedded-media-static PRIVATE ${GSTREAMER_INCLUDE_DIRS} ${GSTAPP_INCLUDE_DIRS})
    target_link_libraries(embedded-media-static ${GSTREAMER_LIBRARIES} ${GSTAPP_LIBRARIES})
    target_include_directories(embedded-media-shared PRIVATE ${GSTREAMER_INCLUDE_DIRS} ${GSTAPP_INCLUDE_DIRS})
    target_link_libraries(embedded-media-shared ${GSTREAMER_LIBRARIES} ${GSTAPP_LIBRARIES})
endif()

if(NOT FRAME_FILE_PATH_PREFIX OR FRAME_FILE_PATH_PREFIX STREQUAL "")
    set(FRAME_FILE_PATH_PREFIX ${CMAKE_CURRENT_LIST_DIR}/resources/frames/)
    message(STATUS "FRAME_FILE_PATH_PREFIX is ${FRAME_FILE_PATH_PREFIX}")
endif()
target_compile_definitions(embedded-media-static PRIVATE "FRAME_FILE_PATH_PREFIX=\"${FRAME_FILE_PATH_PREFIX}\"")
# target_compile_definitions(embedded-media-shared PRIVATE "FRAME_FILE_PATH_PREFIX=\"${FRAME_FILE_PATH_PREFIX}\"")

# include(GNUInstallDirs)

install(TARGETS embedded-media-static
        LIBRARY DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR})

# install(TARGETS embedded-media-shared
#         LIBRARY DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR}
#         ARCHIVE DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR})
# >>>>>>>
#     LIBRARY DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR}
#     ARCHIVE DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR})

install(DIRECTORY ${INCS_DIR}
        DESTINATION ${CMAKE_INSTALL_FULL_INCLUDEDIR})

set(SAMPLES_DIR ${CMAKE_CURRENT_LIST_DIR}/samples/)
set(AWS_DEPENDENCIES_DIR ${CMAKE_BINARY_DIR}/aws-dependencies/)

if(BUILD_WEBRTC_SAMPLES)
    add_subdirectory(${SAMPLES_DIR}/webrtc)
endif()
if(BUILD_KVS_SAMPLES)
    add_subdirectory(${SAMPLES_DIR}/kvs)
endif()
if(BUILD_SAVE_FRAME_SAMPLES)
    add_subdirectory(${SAMPLES_DIR}/saveframe)
endif()
