CMAKE_MINIMUM_REQUIRED( VERSION 3.0.0 )

PROJECT( openddlparser VERSION 0.1.0 )

SET ( openddl_parser_src
  code/OpenDDLCommon.cpp
  code/OpenDDLExport.cpp
  code/OpenDDLParser.cpp
  code/DDLNode.cpp
  code/Value.cpp
  include/openddlparser/OpenDDLCommon.h
  include/openddlparser/OpenDDLExport.h
  include/openddlparser/OpenDDLParser.h
  include/openddlparser/OpenDDLParserUtils.h
  include/openddlparser/DDLNode.h
  include/openddlparser/Value.h
  README.md
  )

ADD_LIBRARY( openddl_parser "${openddl_parser_src}")

include_directories(include)

## install

set(config_install_dir "lib/cmake/${PROJECT_NAME}")
set(include_install_dir "include")

set(generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")

# Configuration
set(version_config "${generated_dir}/${PROJECT_NAME}ConfigVersion.cmake")
set(project_config "${generated_dir}/${PROJECT_NAME}Config.cmake")
set(targets_export_name "${PROJECT_NAME}Targets")
set(namespace "${PROJECT_NAME}::")

include(CMakePackageConfigHelpers)

write_basic_package_version_file(
  "${version_config}" COMPATIBILITY SameMajorVersion
  )

configure_package_config_file(
  "cmake/Config.cmake.in"
  "${project_config}"
  INSTALL_DESTINATION "${config_install_dir}"
  )

install(
  TARGETS openddl_parser
  EXPORT "${targets_export_name}"
  LIBRARY DESTINATION "lib"
  ARCHIVE DESTINATION "lib"
  RUNTIME DESTINATION "bin"
  INCLUDES DESTINATION "${include_install_dir}"
  )

install(
  DIRECTORY include/openddlparser/ # Trailing slash is required
  DESTINATION "${include_install_dir}/${PROJECT_NAME}"
  FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
  )

install(
  FILES "${project_config}" "${version_config}"
  DESTINATION "${config_install_dir}"
  )

install(
  EXPORT "${targets_export_name}"
  NAMESPACE "${namespace}"
  DESTINATION "${config_install_dir}"
  )