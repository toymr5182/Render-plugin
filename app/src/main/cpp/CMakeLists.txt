cmake_minimum_required(VERSION 3.22)
project(xclipse_shim C)

add_library(xclipse_shim SHARED xclipse_shim.c)
target_link_libraries(xclipse_shim log dl)

# libglxshim.so - the GL library the plugin's `renderer` metadata points at.
# The launcher hands its absolute path to LWJGL (-Dorg.lwjgl.opengl.libname), so the APK must
# contain it or the game crashes with "Failed to dynamically load library ... libglxshim.so".
add_library(glxshim SHARED glxshim.c)
target_link_libraries(glxshim log dl)

# libEGL_xclipse.so - the EGL library the plugin's `renderer` metadata points at, the same way
# working launcher plugins own their EGL side. It forwards to the real EGL (Mesa or system) and
# keeps the launcher from parsing a NULL version string.
# The target name carries the exact file-name casing on purpose: the manifest declares
# libEGL_xclipse.so and Android file names are case sensitive, so a lower-case libegl_xclipse.so
# would neither load nor satisfy the packaging check.
add_library(EGL_xclipse SHARED eglshim.c)
target_link_libraries(EGL_xclipse log dl)

# Optional: experimental RADV Xclipse activation via bylaws/libadrenotools.
# CI sets -DXCLIPSE_ADRENOTOOLS_DIR=<checkout of libadrenotools>.
option(XCLIPSE_WITH_ADRENOTOOLS "Link libadrenotools for custom Vulkan driver loading" OFF)
if(XCLIPSE_WITH_ADRENOTOOLS)
    set(XCLIPSE_ADRENOTOOLS_DIR "" CACHE PATH "libadrenotools source dir")
    add_subdirectory(${XCLIPSE_ADRENOTOOLS_DIR} adrenotools)
    target_compile_definitions(xclipse_shim PRIVATE XCLIPSE_WITH_ADRENOTOOLS)
    target_link_libraries(xclipse_shim adrenotools)
endif()
