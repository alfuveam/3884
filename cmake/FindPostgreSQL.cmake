find_path(PostgreSQL_INCLUDE_DIR libpq-fe.h
   /usr/include/pgsql/
   /usr/local/include/pgsql/
   /usr/include/postgresql/
)

find_library(PostgreSQL_LIBRARIES NAMES pq libpq)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(PostgreSQL DEFAULT_MSG
                                  PostgreSQL_INCLUDE_DIR PostgreSQL_LIBRARIES )

mark_as_advanced(PostgreSQL_INCLUDE_DIR PostgreSQL_LIBRARIES)