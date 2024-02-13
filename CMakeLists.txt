# cmake -------------------------------------------------------------------------------------------
cmake_minimum_required( VERSION 3.20 )

set( CMAKE_CXX_STANDARD          20  )
set( CMAKE_CXX_STANDARD_REQUIRED ON  )
set( CMAKE_CXX_EXTENSIONS        OFF )
# are::linux --------------------------------------------------------------------------------------
set( LINUX_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib )
set( LINUX_INC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/inc )
set( LINUX_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src )

set( LINUX_SOURCE 
    ${LINUX_SRC_DIR}/LinuxWindow.cpp
)

set( LINUX_HEADER 
    ${LINUX_INC_DIR}/Graphics/LinuxWindow.h
    ${LINUX_INC_DIR}/Input/LinuxInput.h
)

add_library( linux STATIC 
    ${LINUX_HEADER}
    ${LINUX_SOURCE}
)

target_include_directories( linux
    PUBLIC
        $<BUILD_INTERFACE:${LINUX_INC_DIR}>
	    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries( linux
    PRIVATE
        base
        input
        graphics_interface
)

add_library( aer::linux ALIAS linux )