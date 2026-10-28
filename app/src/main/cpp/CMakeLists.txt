cmake_minimum_required(VERSION 3.22)
project(xclipse_shim C)

add_library(xclipse_shim SHARED xclipse_shim.c)
target_link_libraries(xclipse_shim log dl)

# Optional: experimental RADV Xclipse activation via bylaws/libadrenotools.
# CI sets -DXCLIPSE_ADRENOTOOLS_DIR=<checkout of libadrenotools>.
option(XCLIPSE_WITH_ADRENOTOOLS "Link libadrenotools for custom Vulkan driver loading" OFF)
if(XCLIPSE_WITH_ADRENOTOOLS)
    set(XCLIPSE_ADRENOTOOLS_DIR "" CACHE PATH "libadrenotools source dir")
    add_subdirectory(${XCLIPSE_ADRENOTOOLS_DIR} adrenotools)
    target_compile_definitions(xclipse_shim PRIVATE XCLIPSE_WITH_ADRENOTOOLS)
    target_link_libraries(xclipse_shim adrenotools)
endif()
