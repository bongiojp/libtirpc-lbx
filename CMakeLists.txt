
# Ganesha CMake integration

if(TIRPC_EPOLL)
  add_definitions(-DTIRPC_EPOLL)
endif(TIRPC_EPOLL)

set(MSK_DUMP_FRAGMENTS 1)

include_directories(
  "${PROJECT_BINARY_DIR}/include"
  "${PROJECT_SOURCE_DIR}/libntirpc/tirpc/"
  "${PROJECT_SOURCE_DIR}/include"
  "${OS_INCLUDE_DIR}"
  "${PROJECT_SOURCE_DIR}/libntirpc/src/"
)

add_subdirectory(src)
