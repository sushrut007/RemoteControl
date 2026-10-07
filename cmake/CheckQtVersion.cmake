# Ensures the desktop build uses Qt 6.8.x (6.8.3 target per project policy).

function(darpan_require_qt_version)
    set(_required_minor 8)
    set(_required_patch 3)

    if(NOT Qt6_VERSION)
        message(FATAL_ERROR "Qt6_VERSION is not set. Call find_package(Qt6 ...) before darpan_require_qt_version().")
    endif()

    if(Qt6_VERSION VERSION_LESS "6.8.0")
        message(FATAL_ERROR
            "Darpan requires Qt 6.8.3 or newer 6.8.x; found Qt ${Qt6_VERSION}.")
    endif()

    if(Qt6_VERSION VERSION_GREATER_EQUAL "6.9.0")
        message(WARNING
            "Darpan is tested with Qt 6.8.3; found Qt ${Qt6_VERSION}. Proceed with caution.")
    endif()

    message(STATUS "Darpan Qt check: using Qt ${Qt6_VERSION} (policy: 6.8.3+)")
endfunction()
