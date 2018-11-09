cmake_minimum_required(VERSION 3.12)
project(srs CXX)

INCLUDE_DIRECTORIES(objs
        objs/st objs/hp objs/openssl/include
        src/core src/kernel src/protocol src/app
        src/service src/libs)

set(SOURCE_FILES src/main/srs_main_server.cpp)
AUX_SOURCE_DIRECTORY(src/app SOURCE_FILES)
AUX_SOURCE_DIRECTORY(src/core SOURCE_FILES)
AUX_SOURCE_DIRECTORY(src/kernel SOURCE_FILES)
AUX_SOURCE_DIRECTORY(src/libs SOURCE_FILES)
AUX_SOURCE_DIRECTORY(src/protocol SOURCE_FILES)
AUX_SOURCE_DIRECTORY(src/service SOURCE_FILES)


ADD_DEFINITIONS("-g -O0")

ADD_EXECUTABLE(srs ${SOURCE_FILES})
TARGET_LINK_LIBRARIES(srs dl)
TARGET_LINK_LIBRARIES(srs ${PROJECT_SOURCE_DIR}/objs/st/libst.a)
TARGET_LINK_LIBRARIES(srs ${PROJECT_SOURCE_DIR}/objs/openssl/lib/libssl.a)
TARGET_LINK_LIBRARIES(srs ${PROJECT_SOURCE_DIR}/objs/openssl/lib/libcrypto.a)
TARGET_LINK_LIBRARIES(srs ${PROJECT_SOURCE_DIR}/objs/hp/libhttp_parser.a)
TARGET_LINK_LIBRARIES(srs -ldl)

IF(NOT EXISTS ${PROJECT_SOURCE_DIR}/objs/st/libst.a)
    MESSAGE("srs_libs not found")
    EXEC_PROGRAM("cd .. && ./configure")
ELSE(NOT EXISTS ${PROJECT_SOURCE_DIR}/objs/st/libst.a)
    MESSAGE("srs_libs is ok")
ENDIF(NOT EXISTS ${PROJECT_SOURCE_DIR}/objs/st/libst.a)
