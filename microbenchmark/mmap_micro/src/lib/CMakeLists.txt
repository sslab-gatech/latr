add_subdirectory(util)

set(SOURCES
  main.cc
)

find_package(Threads)

add_executable(mmap_mb ${SOURCES})
target_link_libraries(mmap_mb util ${CMAKE_THREAD_LIBS_INIT})
