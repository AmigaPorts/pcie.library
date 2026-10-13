# SPDX-License-Identifier: MPL-2.0
cmake_minimum_required(VERSION 3.14.0)
project(pcie.library LANGUAGES C VERSION 2.0)

set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

include(CPM-init)

get_verstring(VERSTRING)

if(NOT TARGET devicetree)
    CPMGetPackage(devicetree.resource)
endif()

if(NOT TARGET interrupt)
    CPMGetPackage(interrupt.library)
endif()

# Set default install prefix to ./install subdirectory
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/install" CACHE PATH "Install path" FORCE)
endif()

sfdc(sfd/pcie.sfd)
target_include_directories(pcie INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(pcie.library
    src/devicetree_address.c
    src/devicetree_interrupt.c
    src/main.c
    src/dma.c
    src/dma_address.c
    src/dma_acpi.c
    src/dma_discovery.c
    src/dma_cache.c
    src/io.c
    src/ownership.c
    src/resource.c
    src/region.c
    src/config.c
    src/address.c
    src/interrupt.c
    src/interrupt_prepare.c
    src/interrupt_server.c
    src/intx.c
    src/msi.c
    src/msi_mask.c
    src/msi_program.c
    src/msix_program.c
    src/message_allocation.c
    src/interrupt_allocation.c
    src/interrupt_program.c
    src/message_server.c
    src/interrupt_callback.c
    src/interrupt_vectors.c
    src/routing.c
    src/enumeration.c
    src/probe.c
    src/capability.c
    src/mapping.c
    src/attributes.c
    src/attribute_get.c
    src/window.c
    src/device.c
    src/catalog.c
    src/find.c
    src/board.c
    src/prepare.c
    src/command.c
    src/host.c
    src/host_dma.c
    src/shared_dma.c
    src/board_dma.c
    src/state.c
    src/reset.c
    src/end.c
)

target_include_directories(pcie.library PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include/private)
target_compile_definitions(pcie.library PRIVATE VERSION_STRING="${VERSTRING}" LIBRARY_VERSION=${PROJECT_VERSION_MAJOR} LIBRARY_REVISION=${PROJECT_VERSION_MINOR} LIBRARY_NAME="${PROJECT_NAME}")
set_property(TARGET pcie.library APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ldscript.lds")
add_dependencies(pcie.library devicetree pcie interrupt)
target_link_libraries(pcie.library pcie devicetree interrupt)
target_link_options(pcie.library PRIVATE -nostdlib -nostartfiles -Wall -Wextra -s -Wl,-T${CMAKE_CURRENT_SOURCE_DIR}/ldscript.lds -Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/pcie.library.map)
target_compile_options(pcie.library PUBLIC $<$<COMPILE_LANGUAGE:C>:-O2 -m68040 -fomit-frame-pointer -ffreestanding -fno-builtin -fno-common -Wall -Wextra -Werror>)
bin_to_header(pcie.library)

# Set up information for Aminet
add_project_info(
    "pcie.library for AmigaOS 3.x"
    "marlon@amigadev.com (Marlon Beijer)"
    "driver/other"
    "m68k-amigaos"
)

add_aminet_readme_generator(pcie.library "pcie.library.readme")

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/sfd/pcie.sfd DESTINATION Developer/sfd)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/libraries DESTINATION Developer/include)
install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/include/ DESTINATION Developer/include)
install(FILES LICENSE DESTINATION Developer)
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/pcie.library DESTINATION Libs)
