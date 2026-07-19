include_guard(GLOBAL)

get_filename_component(_process_communication_root "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)

if (NOT EXISTS "${_process_communication_root}/src/TCPTransport.hpp")
    message(FATAL_ERROR
            "process_communication: could not locate the 'src' headers "
            "relative to ${CMAKE_CURRENT_LIST_FILE}")
endif ()

if (NOT TARGET process_communications)
    add_library(process_communications INTERFACE)
    add_library(lib::process_communications ALIAS process_communications)

    target_include_directories(process_communications INTERFACE
            "$<BUILD_INTERFACE:${_process_communication_root}/src>")

    target_compile_features(process_communications INTERFACE cxx_std_23)
endif ()

unset(_process_communication_root)
