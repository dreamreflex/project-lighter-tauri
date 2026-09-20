# A workspace may inherit restrictive/shared ACLs. Those belong to the
# workspace, not to /usr in the distributable archive. Only touch CPack staging.
if(NOT IS_DIRECTORY "${CPACK_TEMPORARY_DIRECTORY}" OR NOT CPACK_TEMPORARY_DIRECTORY MATCHES "/_CPack_Packages/")
  message(FATAL_ERROR "Unexpected package staging directory")
endif()
find_program(SETFACL_EXECUTABLE setfacl)
if(SETFACL_EXECUTABLE)
  execute_process(COMMAND "${SETFACL_EXECUTABLE}" -Rb "${CPACK_TEMPORARY_DIRECTORY}"
    COMMAND_ERROR_IS_FATAL ANY)
endif()
file(GLOB_RECURSE entries LIST_DIRECTORIES true "${CPACK_TEMPORARY_DIRECTORY}/*")
foreach(entry IN LISTS entries)
  if(IS_DIRECTORY "${entry}" OR entry MATCHES "/bin/lighter$")
    file(CHMOD "${entry}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
  else()
    file(CHMOD "${entry}" PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
  endif()
endforeach()
