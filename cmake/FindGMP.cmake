# Locate GMP library
# This module defines
#   GMP_FOUND
#   GMP_INCLUDE_DIR
#   GMP_LIBRARIES

find_path(GMP_INCLUDE_DIR NAMES gmp.h
    PATH_SUFFIXES include)
find_library(GMP_LIBRARIES NAMES gmp libgmp mpir
    PATH_SUFFIXES lib)
find_library(GMPXX_LIBRARIES NAMES gmpxx libgmpxx mpir
    PATH_SUFFIXES lib)

include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(GMP DEFAULT_MSG GMP_INCLUDE_DIR GMP_LIBRARIES GMPXX_LIBRARIES)

mark_as_advanced(GMP_INCLUDE_DIR GMP_LIBRARIES GMPXX_LIBRARIES)
