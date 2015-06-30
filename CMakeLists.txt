project(memdir C)
set(CMAKE_BUILD_TYPE Debug)

include(CheckFunctionExists)

# custom module path
set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake/modules)

find_package(GLIB2 REQUIRED)
include_directories(${GLIB2_INCLUDE_DIR})

# see if we have gbase64 in glib
set(CMAKE_REQUIRED_LIBRARIES ${GLIB2_LIBRARIES})
set(CMAKE_REQUIRED_INCLUDES ${GLIB2_INCLUDE_DIR})
check_function_exists(g_base64_decode HAVE_GBASE64)
if    ( HAVE_GBASE64 )
  add_definitions(-DHAVE_GBASE64)
endif ( HAVE_GBASE64 )

include_directories(${CMAKE_SOURCE_DIR})

# POSIX
add_definitions(-D_POSIX_SOURCE=1)

# Compiler flags
if(CMAKE_COMPILER_IS_GNUCC)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -W -Werror -ansi -pedantic -std=c99")
  set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -g -pg -DDEBUG")
  set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3")
endif(CMAKE_COMPILER_IS_GNUCC)

if(MSVC)
  # /Za would be nice but it breaks windows.h
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /W4 /WX")
  set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} /Zi /DDEBUG")
  set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /O2")
endif(MSVC)


add_executable(test2 test.c misc.c memdir.c test/memdir.c)
target_link_libraries(test2 ${LIBEVENT_LIBRARIES} ${GLIB2_LIBRARIES})
set(TEST2_EXTRA_FLAGS "${TEST2_EXTRA_FLAGS} -DUNITTEST")
if (NOT WIN32)
  target_link_libraries(test2 -lm)
  set(TEST2_EXTRA_FLAGS "${TEST2_EXTRA_FLAGS} -DUSE_COLOR")
endif (NOT WIN32)
if (CMAKE_COMPILER_IS_GNUCC)
  #  set(TEST2_EXTRA_FLAGS "${TEST_EXTRA_FLAGS} -fprofile-arcs -ftest-coverage")
  #  target_link_libraries(test2 "-lgcov")
endif (CMAKE_COMPILER_IS_GNUCC)
set_target_properties(test2 PROPERTIES COMPILE_FLAGS ${TEST2_EXTRA_FLAGS})
