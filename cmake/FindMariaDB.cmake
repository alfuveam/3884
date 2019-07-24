#http://cmake.3232098.n2.nabble.com/Linking-to-MySQL-C-Connector-libraries-using-extra-flags-Ubuntu-14-04-LTS-gcc-td7590422.html

if (WIN32)
    if (CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(MARIADB_WIN_PATH "C:/Program Files/MariaDB/MariaDB C Client Library 64-bit")
    else ()
        set(MARIADB_WIN_PATH "C:/Program Files (x86)/MariaDB/MariaDB C Client Library"
                             "C:/Program Files/MariaDB/MariaDB C Client Library")
    endif ()
endif ()

# Look for the header file.
find_path(MARIADB_INCLUDE_DIR NAMES mysql.h
          PATH_SUFFIXES include mariadb include/mariadb
          PATHS /usr/include/mariadb ${MARIADB_WIN_PATH})

# Look for the library.
find_library(MARIADB_LIBRARY NAMES mariadb libmariadb
                                   mariadbclient libmariadbclient
             PATH_SUFFIXES lib mariadb lib/mariadb
             PATHS ${MARIADB_WIN_PATH})

# Find version string.
if (MARIADB_INCLUDE_DIR AND EXISTS "${MARIADB_INCLUDE_DIR}/mysql_version.h")
    file(STRINGS "${MARIADB_INCLUDE_DIR}/mysql_version.h" mariadb_version_str
         REGEX "^#define[\t ]+MARIADB_PACKAGE_VERSION[\t ]+\".*\"")
    string(REGEX REPLACE "^#define[\t ]+MARIADB_PACKAGE_VERSION[\t ]+\"(.*)\".*"
           "\\1" MARIADB_VERSION "${mariadb_version_str}")
endif ()

# handle the find_package args and set MYSQL_FOUND
include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(MARIADB
    REQUIRED_VARS MARIADB_LIBRARY MARIADB_INCLUDE_DIR
    VERSION_VAR MARIADB_VERSION)

mark_as_advanced(MARIADB_INCLUDE_DIR MARIADB_LIBRARY)
