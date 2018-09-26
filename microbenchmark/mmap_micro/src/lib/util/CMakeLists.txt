set(SOURCES
  util.cc
  runnable.cc
)

add_library(util STATIC ${SOURCES})

target_link_libraries (util rt)
