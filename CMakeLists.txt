cmake_minimum_required(VERSION 3.20)
project(mxl-multi-compositor CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED IMPORTED_TARGET gstreamer-1.0 gstreamer-app-1.0)

# libnvnmos provides the NMOS Node (IS-04, IS-05, BCP-007-03). NVNMOS_ROOT is
# its install prefix: include/nvnmos.h and lib/libnvnmos.so.
set(NVNMOS_ROOT "" CACHE PATH "libnvnmos install prefix")
find_path(NVNMOS_INCLUDE_DIR nvnmos.h HINTS ${NVNMOS_ROOT}/include REQUIRED)
find_library(NVNMOS_LIBRARY nvnmos HINTS ${NVNMOS_ROOT}/lib REQUIRED)

add_library(nmos-support STATIC src/nmos/domain.cpp)
target_include_directories(nmos-support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(nmos-support PRIVATE -Wall -Wextra -Wpedantic)

add_executable(mxl-multi-compositor src/main.cpp src/nmos/node.cpp)
target_compile_options(mxl-multi-compositor PRIVATE -Wall -Wextra -Wpedantic)
target_include_directories(mxl-multi-compositor PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/vendor ${CMAKE_CURRENT_SOURCE_DIR}/src ${NVNMOS_INCLUDE_DIR})
target_link_libraries(mxl-multi-compositor PRIVATE
  PkgConfig::GST mxl nmos-support ${NVNMOS_LIBRARY} pthread)

install(TARGETS mxl-multi-compositor
        RUNTIME DESTINATION bin)

enable_testing()
add_executable(domain_test tests/domain_test.cpp)
target_link_libraries(domain_test PRIVATE nmos-support)
add_test(NAME domain_test COMMAND domain_test)
