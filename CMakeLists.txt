cmake_minimum_required(VERSION 2.8)

### INTERPROCEDURAL_OPTIMIZATION ###
cmake_policy(SET CMP0069 NEW)
### END INTERPROCEDURAL_OPTIMIZATION ###

set(CMAKE_DISABLE_SOURCE_CHANGES ON)
set(CMAKE_DISABLE_IN_SOURCE_BUILD ON)

project(tfs)

add_subdirectory(src)
add_executable(tfs ${tfs_SRC})

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set_target_properties(tfs PROPERTIES CXX_STANDARD 11)
set_target_properties(tfs PROPERTIES CXX_STANDARD_REQUIRED ON)

if (${CMAKE_VERSION} VERSION_GREATER "3.16.0")
    target_precompile_headers(tfs PUBLIC src/otpch.h)
else ()
    include(cotire)
    set_target_properties(tfs PROPERTIES COTIRE_CXX_PREFIX_HEADER_INIT "src/otpch.h")
    set_target_properties(tfs PROPERTIES COTIRE_ADD_UNITY_BUILD FALSE)
    cotire(tfs)
endif ()

if (NOT WIN32)
    add_compile_options(-Wall -Werror -pipe -fvisibility=hidden)
endif ()

set(CMAKE_CXX_FLAGS_PERFORMANCE "${CMAKE_CXX_FLAGS_RELEASE} -march=native")

if (CMAKE_COMPILER_IS_GNUCXX)
    add_compile_options(-fno-strict-aliasing)
endif ()

# Find packages.
find_package(CryptoPP QUIET)
if (CryptoPP_FOUND)  # vcpkg-provided cmake package called CryptoPP
    set(Crypto++_LIBRARIES "cryptopp-static")
else()
    find_package(Crypto++ REQUIRED)
endif ()

# Find packages.
find_package(PugiXML REQUIRED)
find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)

option(USE_LUAJIT "Use LuaJIT" OFF)

# Selects LuaJIT if user defines or auto-detected
if (DEFINED USE_LUAJIT AND NOT USE_LUAJIT)
    set(FORCE_LUAJIT ${USE_LUAJIT})
else ()
    find_package(LuaJIT)
    set(FORCE_LUAJIT ${LuaJIT_FOUND})
endif ()

option(USE_LUAJIT "Use LuaJIT" ${FORCE_LUAJIT})

if (FORCE_LUAJIT)
    if (APPLE)
        set(CMAKE_EXE_LINKER_FLAGS "-pagezero_size 10000 -image_base 100000000")
    endif ()
else ()
    find_package(Lua REQUIRED)
endif ()

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
option(__EXCEPTION_TRACER__ "__EXCEPTION_TRACER__" OFF)
option(__TRACK_NETWORK__ "__TRACK_NETWORK__" OFF)
option(__WAR_SYSTEM__ "__WAR_SYSTEM__" OFF)
option(__FILESYSTEM_HIERARCHY_STANDARD__ "__FILESYSTEM_HIERARCHY_STANDARD__" OFF)
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
  target_link_libraries(tfs PRIVATE ${MYSQL_CLIENT_LIBS})
endif()

if(__USE_SQLITE__)
  add_definitions(-D__USE_SQLITE__)
  find_package(SQLite REQUIRED)
  include_directories(${SQLITE_INCLUDE_DIR})
  target_link_libraries(tfs PRIVATE ${SQLITE_LIBRARY})
endif()

if(__USE_ODBC__)
  add_definitions(-D__USE_ODBC__)
  find_package(ODBC REQUIRED)
  include_directories(${ODBC_INCLUDE_DIRS})
  target_link_libraries(tfs PRIVATE ${ODBC_LIBRARIES})
endif()

if(__USE_PGSQL__)
  add_definitions(-D__USE_PGSQL__)
  find_package(PostgreSQL REQUIRED)
  include_directories(${PostgreSQL_INCLUDE_DIRS})
  target_link_libraries(tfs PRIVATE ${PostgreSQL_LIBRARIES})
endif()

find_package(Boost 1.53.0 REQUIRED COMPONENTS system iostreams filesystem)

include_directories(${LUA_INCLUDE_DIR} ${Boost_INCLUDE_DIRS} ${PUGIXML_INCLUDE_DIR} ${Crypto++_INCLUDE_DIR} ${ZLIB_INCLUDE_DIR})
target_link_libraries(tfs PRIVATE
        Boost::system
        Boost::filesystem
        Boost::iostreams
        ${LUA_LIBRARIES} 
        ${PUGIXML_LIBRARIES} 
        ${Crypto++_LIBRARIES} 
        ${CMAKE_THREAD_LIBS_INIT}
        ${ZLIB_LIBRARY}
        )

### INTERPROCEDURAL_OPTIMIZATION ###
include(CheckIPOSupported)
check_ipo_supported(RESULT result OUTPUT error)
if (result)
    message(STATUS "IPO / LTO enabled")
    set_target_properties(tfs PROPERTIES INTERPROCEDURAL_OPTIMIZATION True)
else ()
    message(STATUS "IPO / LTO not supported: <${error}>")
endif ()
### END INTERPROCEDURAL_OPTIMIZATION ###

### Git Version ###
# Define the two required variables before including
# the source code for watching a git repository.
set(PRE_CONFIGURE_FILE "cmake/gitmetadata.h.in")
set(POST_CONFIGURE_FILE "${CMAKE_CURRENT_BINARY_DIR}/gitmetadata.h")
include(git_watcher)
if (Git_FOUND)
    add_dependencies(tfs check_git)
    target_include_directories(tfs PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
endif()
### END  Git Version ###