set(LIBRARY_NAME "fmi")

set(CMAKE_C_FLAGS "-m32 -g -O0")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -D_PAYLOAD_EXPORTS")

include_directories($ENV{RIVER_SDK_DIR}/include)

add_library(${LIBRARY_NAME} SHARED
	fmi.c
	Payload.cpp
	)

install(TARGETS ${LIBRARY_NAME} DESTINATION ${CMAKE_INSTALL_PREFIX}/lib)

