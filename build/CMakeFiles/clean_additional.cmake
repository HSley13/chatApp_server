# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "Debug")
  file(REMOVE_RECURSE
  "CMakeFiles/server_app_autogen.dir/AutogenUsed.txt"
  "CMakeFiles/server_app_autogen.dir/ParseCache.txt"
  "database/CMakeFiles/database_library_autogen.dir/AutogenUsed.txt"
  "database/CMakeFiles/database_library_autogen.dir/ParseCache.txt"
  "database/database_library_autogen"
  "server_app_autogen"
  )
endif()
