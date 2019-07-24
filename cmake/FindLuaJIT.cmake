# Locate LuaJIT library
# This module defines
#  LUAJIT_FOUND, if false, do not try to link to Lua
#  LUA_LIBRARIES
#  LUA_INCLUDE_DIR, where to find lua.h
#  LUAJIT_VERSION_STRING, the version of Lua found (since CMake 2.8.8)

## Copied from default CMake FindLua51.cmake 
if(NOT CMAKE_VS_MSBUILD_COMMAND)
  find_path(LUA_INCLUDE_DIR NAMES luajit.h
    HINTS
      ENV LUA_DIR
    PATH_SUFFIXES include include/luajit-2.0
    PATHS
    ~/Library/Frameworks
    /Library/Frameworks
    /sw # Fink
    /opt/local # DarwinPorts
    /opt/csw # Blastwave
    /opt
  )

  find_library(LUA_LIBRARY
    NAMES luajit-5.1 luajit
    HINTS
      ENV LUA_DIR
    PATH_SUFFIXES lib
    PATHS
    ~/Library/Frameworks
    /Library/Frameworks
    /sw
    /opt/local
    /opt/csw
    /opt
  )
else()
  find_path(LUA_INCLUDE_DIR NAMES luajit.h)
  find_library(LUA_LIBRARY NAMES luajit)
endif()

if(LUA_LIBRARY)
  # include the math library for Unix
  if(UNIX AND NOT APPLE)  
    find_library(LUA_MATH_LIBRARY m)
    set( LUA_LIBRARIES "${LUA_LIBRARY};${LUA_MATH_LIBRARY}" CACHE STRING "Lua Libraries")
  # For Windows and Mac, don't need to explicitly include the math library
  else()
    set( LUA_LIBRARIES "${LUA_LIBRARY}" CACHE STRING "Lua Libraries")
  endif()
endif()

if(LUA_INCLUDE_DIR AND EXISTS "${LUA_INCLUDE_DIR}/luajit.h")  
  if(NOT UNIX AND NOT APPLE)
    file(STRINGS "${LUA_INCLUDE_DIR}/luajit.h" luajit_version_str REGEX "^#define[ \t]+LUAJIT_VERSION[ \t]+\"LuaJIT .+\"")

    string(REGEX REPLACE "^#define[ \t]+LUAJIT_VERSION[ \t]+\"LuaJIT ([^\"]+)\".*" "\\1" LUAJIT_VERSION_STRING "${luajit_version_str}")
    unset(luajit_version_str)
  endif()
endif()

if(APPLE)
  set(CMAKE_EXE_LINKER_FLAGS "-pagezero_size 10000 -image_base 100000000")
endif()

include(FindPackageHandleStandardArgs)
# handle the QUIETLY and REQUIRED arguments and set LUA_FOUND to TRUE if
# all listed variables are TRUE
FIND_PACKAGE_HANDLE_STANDARD_ARGS(LuaJIT 
    REQUIRED_VARS LUA_LIBRARIES LUA_INCLUDE_DIR 
    VERSION_VAR LUAJIT_VERSION_STRING)

mark_as_advanced(LUA_INCLUDE_DIR LUA_LIBRARIES LUA_LIBRARY LUA_MATH_LIBRARY)
