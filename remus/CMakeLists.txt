
add_subdirectory(testing)

#build the base common library
add_subdirectory(common)

#build the base common protocol library
#that all three components use to talk to each other
add_subdirectory(proto)


add_subdirectory(client)
add_subdirectory(server)
add_subdirectory(worker)

set(headers
  Version.h
  )

remus_public_headers(${headers})

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/RemusConfig.cmake.in"
  "${Remus_BINARY_DIR}/RemusConfig.cmake"
  @ONLY)

if (WIN32)
  set(cmakedir cmake)
else ()
  set(cmakedir lib/cmake)
endif ()

#setup the exports for the library when used from an installed location
install(EXPORT Remus-targets DESTINATION ${cmakedir}/Remus)
install(
  FILES       "${Remus_BINARY_DIR}/RemusConfig.cmake"
  DESTINATION ${cmakedir}/Remus)
