cmake_minimum_required(VERSION 2.8)

set(CMAKE_DISABLE_SOURCE_CHANGES ON)
set(CMAKE_DISABLE_IN_SOURCE_BUILD ON)

project(tfs)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(cotire)

add_compile_options(-Wall -Werror -pipe -fvisibility=hidden)
#-lz ; to libxml2
#-lcrypto ; to openssl
link_libraries(-lz -lcrypto -lmysqlclient -lxml2)

set(CMAKE_CXX_FLAGS_PERFORMANCE "${CMAKE_CXX_FLAGS_RELEASE} -march=native")

if (CMAKE_COMPILER_IS_GNUCXX)
    add_compile_options(-fno-strict-aliasing)
endif()


include(FindCXX)
include(FindLTO)

# Find packages.
find_package(GMP REQUIRED)
find_package(LibXml2 REQUIRED)
find_package(Threads)

# Selects LuaJIT if user defines or auto-detected
if(DEFINED USE_LUAJIT AND NOT USE_LUAJIT)
    set(FORCE_LUAJIT ${USE_LUAJIT})
else()
    find_package(LuaJIT)
    set(FORCE_LUAJIT ${LuaJIT_FOUND})
endif()
option(USE_LUAJIT "Use LuaJIT" ${FORCE_LUAJIT})

if(FORCE_LUAJIT)
    if(APPLE)
      set(CMAKE_EXE_LINKER_FLAGS "-pagezero_size 10000 -image_base 100000000")
    endif()
else()
    find_package(Lua REQUIRED)
endif()

#	Is necessary to build a correct build file
find_package(Boost 1.53.0 COMPONENTS system iostreams filesystem regex date_time thread REQUIRED)

add_subdirectory(src)
add_executable(tfs ${tfs_SRC})

option(__ENABLE_SERVER_DIAGNOSTIC__ "__ENABLE_SERVER_DIAGNOSTIC__" ON)
option(__LOGIN_SERVER__ "__LOGIN_SERVER__" OFF)
option(__OTADMIN__ "__OTADMIN__" OFF)
option(__OTSERV_ALLOCATOR__ "__OTSERV_ALLOCATOR__" OFF)
option(__HOMEDIR_CONF__ "__HOMEDIR_CONF__" OFF)
option(__ROOT_PERMISSION__ "__ROOT_PERMISSION__" ON)
option(__USE_MYSQL__ "__USE_MYSQL__" ON)
option(__USE_PGSQL__ "__USE_PGSQL__" ON)
option(__USE_SQLITE__ "__USE_SQLITE__" ON)
option(__USE_ODBC__ "__USE_ODBC__" ON)
#option(__MULTI_SQL_DRIVERS__ "__MULTI_SQL_DRIVERS__" OFF)
option(__USE_ZLIB__ "__USE_ZLIB__" OFF)
option(__EXCEPTION_TRACER__ "__EXCEPTION_TRACER__" OFF)
option(__TRACK_NETWORK__ "__TRACK_NETWORK__" OFF)
option(__WAR_SYSTEM__ "__WAR_SYSTEM__" OFF)
option(__NO_BOOST_EXCEPTIONS__ "__NO_BOOST_EXCEPTIONS__" OFF)
option(__DEBUG__ "__DEBUG__" OFF)

if(__DEBUG__)
	add_definitions(-D__DEBUG__ -D__DEBUG_MOVESYS__ -D__DEBUG_CHAT__ -D__DEBUG_EXCEPTION_REPORT__ -D__DEBUG_HOUSES__ -D__DEBUG_LUASCRIPTS__ -D__DEBUG_MAILBOX__ -D__DEBUG_NET__ -D__DEBUG_NET_DETAIL__ -D__DEBUG_RAID__ -D__DEBUG_SCHEDULER__ -D__DEBUG_SPAWN__ -D__SQL_QUERY_DEBUG__)
endif()

# Make sure at least one database driver is selected
if(NOT __USE_MYSQL__ AND NOT __USE_SQLITE__ AND NOT __USE_ODBC__ AND NOT __USE_PGSQL__)
  message(FATAL_ERROR "At least one database driver has to be selected.")
endif()

if(__USE_MYSQL__)
 add_definitions(-D__USE_MYSQL__)
  find_package(MySQL REQUIRED)
  include_directories(${MYSQL_INCLUDE_DIR})
  target_link_libraries(tfs ${MYSQL_LIBRARY})
endif()

if(__USE_SQLITE__)
 add_definitions(-D__USE_SQLITE__)
  find_package(SQLite REQUIRED)
  include_directories(${SQLITE_INCLUDE_DIR})
  target_link_libraries(tfs ${SQLITE_LIBRARY})
endif()

if(__USE_ODBC__)
 add_definitions(-D__USE_ODBC__)
  find_package(ODBC REQUIRED)
  include_directories(${ODBC_INCLUDE_DIR})
  target_link_libraries(tfs ${ODBC_LIBRARIES})
endif()

if(__USE_PGSQL__)
 add_definitions(-D__USE_PGSQL__)
  find_package(PostgreSQL REQUIRED)
  include_directories(${POSTGRESQL_INCLUDE_DIR})
  target_link_libraries(tfs ${POSTGRESQL_LIBRARIES})
endif()

include_directories(${LUA_INCLUDE_DIR} ${Boost_INCLUDE_DIRS} ${LIBXML2_INCLUDE_DIR} ${GMP_INCLUDE_DIR})
target_link_libraries(tfs ${LUA_LIBRARIES} ${Boost_LIBRARIES} ${LIBXML2_LIBRARY} ${GMP_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})

set_target_properties(tfs PROPERTIES COTIRE_CXX_PREFIX_HEADER_INIT "src/otpch.h")
set_target_properties(tfs PROPERTIES COTIRE_ADD_UNITY_BUILD FALSE)
cotire(tfs)