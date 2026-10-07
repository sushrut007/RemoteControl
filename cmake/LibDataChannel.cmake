# Prebuilt libdatachannel (headers + import lib + DLL on Windows).
# Vendored under third_party/; Qt is not included here.

get_filename_component(_darpan_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

set(DARPAN_LIBDATACHANNEL_ROOT
    "${_darpan_root}/third_party/libdatachannel"
    CACHE PATH "Path to prebuilt libdatachannel (include/ and bin/)")

set(_ldc_include "${DARPAN_LIBDATACHANNEL_ROOT}/include")
set(_ldc_lib "${DARPAN_LIBDATACHANNEL_ROOT}/bin/datachannel.lib")
set(_ldc_dll "${DARPAN_LIBDATACHANNEL_ROOT}/bin/datachannel.dll")

if(NOT EXISTS "${_ldc_include}/rtc/rtc.hpp")
    message(FATAL_ERROR
        "libdatachannel headers not found at ${_ldc_include}\n"
        "Expected vendored layout: third_party/libdatachannel/include and bin/. "
        "See third_party/README.md.")
endif()
if(NOT EXISTS "${_ldc_lib}")
    message(FATAL_ERROR "libdatachannel import library not found at ${_ldc_lib}")
endif()
if(WIN32 AND NOT EXISTS "${_ldc_dll}")
    message(FATAL_ERROR "libdatachannel DLL not found at ${_ldc_dll}")
endif()

add_library(darpan_libdatachannel SHARED IMPORTED GLOBAL)
set_target_properties(darpan_libdatachannel PROPERTIES
    IMPORTED_IMPLIB "${_ldc_lib}"
    IMPORTED_LOCATION "${_ldc_dll}"
    INTERFACE_INCLUDE_DIRECTORIES "${_ldc_include}"
)

function(darpan_link_libdatachannel target)
    target_link_libraries(${target} PRIVATE darpan_libdatachannel)
    if(WIN32)
        target_link_libraries(${target} PRIVATE ws2_32 bcrypt iphlpapi)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_ldc_dll}"
                "$<TARGET_FILE_DIR:${target}>"
            COMMENT "Copy datachannel.dll next to ${target}"
        )
    endif()
endfunction()
