cmake_minimum_required (VERSION 2.6)


project ("varnish-influxdb")

set (VERSION_MAJOR 0)
set (VERSION_MINOR 1)

# set (CMAKE_BUILD_TYPE Release)
add_definitions(-Wall -Werror -pthread)

set (CMAKE_VERBOSE_MAKEFILE true)

# Keep RPATH to /usr/lib/varnish(-plus)/ on installed binaries.
set (CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

find_package(PkgConfig)
pkg_check_modules(VARNISH REQUIRED varnishapi)

if(${VARNISH_VERSION} VERSION_LESS 4.1 )
	message(FATAL_ERROR "Varnish 4.1 or newer is required")
endif()

# Look up keys in pkg-config that pkg_check_modules() doesn't populate.
find_program(PKGCONFIG_EXECUTABLE NAMES pkg-config )

#message(STATUS "is " ${VARNISH_VMODTOOL} " end")
message(STATUS "is " ${VARNISH_LIBDIR} " end")

add_definitions(${VARNISH_CFLAGS})

include_directories("${PROJECT_BINARY_DIR}")
include_directories("src")

add_executable(influxstat src/influxstat.c)

# we need VSB_* from libvarnish, but no pkg-config for it.
target_link_libraries(influxstat /usr/lib/varnish-plus/libvarnish.so)

target_link_libraries(influxstat m)
target_link_libraries(influxstat ${VARNISH_LIBRARIES})

install (FILES influxstat RENAME varnish-influxstat DESTINATION bin)
