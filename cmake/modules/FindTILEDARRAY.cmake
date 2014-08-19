if (TILEDARRAY_INCLUDE_DIRS)
  set (TILEDARRAY_FOUND TRUE)

else (TILEDARRAY_INCLUDE_DIRS)
  set (TILEDARRAY_FOUND FALSE)

  if(TILEDARRAY_INSTALL_DIR)
    set(_TILEDARRAY_INSTALL_DIR ${TILEDARRAY_INSTALL_DIR})
  endif(TILEDARRAY_INSTALL_DIR)

  set(TILEDARRAY_INC_SEARCH_DIR ${_TILEDARRAY_INSTALL_DIR}/include)
  message(STATUS "Searching for includes in ${TILEDARRAY_INC_SEARCH_DIR}")

  find_path(TILEDARRAY_INCLUDE_DIR NAMES tiledarray.h HINTS ${TILEDARRAY_INC_SEARCH_DIR})
   message(STATUS "TiledArray includes ${TILEDARRAY_INCLUDE_DIR}")

  mark_as_advanced(TILEDARRAY_INCLUDE_DIR)

  set(_TILEDARRAY_LIB_NAMES
    "MADlinalg"
    "MADmisc"
    "MADmuparser"
    "MADtensor"
    "MADtinyxml"
    "MADworld"
    "elemental"
    "pmrrr"
    )

  set(_TILEDARRAY_LIBRARY_DIR ${_TILEDARRAY_INSTALL_DIR}/lib)
  set(TILEDARRAY_LIBRARY "")

  foreach(_lib ${_TILEDARRAY_LIB_NAMES})
    set(current_lib NOTFOUND)
    find_library(current_lib ${_lib} HINTS ${_TILEDARRAY_LIBRARY_DIR})
    LIST(APPEND TILEDARRAY_LIBRARY ${current_lib})
  endforeach()

  set(TILEDARRAY_LIBRARY_DIR ${_TILEDARRAY_LIBRARY_DIR})
  mark_as_advanced(TILEDARRAY_LIBRARY)

  if(TILEDARRAY_INCLUDE_DIR)
    if(TILEDARRAY_LIBRARY)
      set(TILEDARRAY_FOUND TRUE)
      set(TILEDARRAY_LIBRARIES ${TILEDARRAY_LIBRARY})
      set(TILEDARRAY_INCLUDE_DIRS ${TILEDARRAY_INCLUDE_DIR})
      set(TILEDARRAY_LIBRARY_DIRS ${TILEDARRAY_LIBRARY_DIR})
      mark_as_advanced(TILEDARRAY_INCLUDE_DIRS TILEDARRAY_LIBRARY_DIRS TILEDARRAY_LIBRARIES)
      message(STATUS "Found TiledArray libraries: ${TILEDARRAY_LIBRARIES}")
    endif(TILEDARRAY_LIBRARY)
  endif(TILEDARRAY_INCLUDE_DIR)

endif(TILEDARRAY_INCLUDE_DIRS)