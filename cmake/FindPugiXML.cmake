find_path(PUGIXML_INCLUDE_DIR NAMES pugixml.hpp
    PATH_SUFFIXES include pugixml include/pugixml-1.8)

find_library(PUGIXML_LIBRARIES NAMES pugixml
    PATH_SUFFIXES lib pugixml lib/pugixml-1.8)

include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(PugiXML REQUIRED_VARS PUGIXML_INCLUDE_DIR PUGIXML_LIBRARIES)

mark_as_advanced(PUGIXML_INCLUDE_DIR PUGIXML_LIBRARIES)
