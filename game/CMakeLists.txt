# We define our own compilation flags here.
set(CMAKE_CXX_STANDARD 14)

# Set default compile flags for GCC
# optimization level can be set here. Note that game/ overwrites this for building game C++ code.
if(CMAKE_COMPILER_IS_GNUCXX)
    message(STATUS "GCC detected, adding compile flags")
    set(CMAKE_CXX_FLAGS 
        "${CMAKE_CXX_FLAGS} \
        -ggdb \
        -Wall \
				-Winit-self \
        -Wextra \
        -Wcast-align \
        -Wcast-qual \
        -Wdisabled-optimization \
        -Wformat=2 \
        -Wmissing-include-dirs \
        -Woverloaded-virtual \
        -Wredundant-decls \
        -Wshadow \
        -Wsign-promo")
else()
		set(CMAKE_CXX_FLAGS "/EHsc")
endif(CMAKE_COMPILER_IS_GNUCXX)

enable_language(ASM_NASM)
set(CMAKE_ASM_NASM_SOURCE_FILE_EXTENSIONS ${CMAKE_ASM_NASM_SOURCE_FILE_EXTENSIONS} asm)
set(CMAKE_ASM_NASM_COMPILE_OBJECT "<CMAKE_ASM_NASM_COMPILER> <INCLUDES> <FLAGS> -f ${CMAKE_ASM_NASM_OBJECT_FORMAT} -o <OBJECT> <SOURCE>")
set_source_files_properties(kernel/asm_funcs.asm PROPERTIES COMPILE_FLAGS "-g")
set(RUNTIME_SOURCE
        runtime.cpp
        system/SystemThread.cpp
        system/IOP_Kernel.cpp
        system/iop_thread.cpp
        system/Deci2Server.cpp
        sce/libcdvd_ee.cpp
        sce/libscf.cpp
        sce/deci2.cpp
        sce/sif_ee.cpp
        sce/iop.cpp
        sce/stubs.cpp
        kernel/asm_funcs.asm
        kernel/fileio.cpp
        kernel/kboot.cpp
        kernel/kdgo.cpp
        kernel/kdsnetm.cpp
        kernel/klink.cpp
        kernel/klisten.cpp
        kernel/kmachine.cpp
        kernel/kmalloc.cpp
        kernel/kmemcard.cpp
        kernel/kprint.cpp
        kernel/kscheme.cpp
        kernel/ksocket.cpp
        kernel/ksound.cpp
        overlord/dma.cpp
        overlord/fake_iso.cpp
        overlord/iso.cpp
        overlord/iso_api.cpp
        overlord/iso_cd.cpp
        overlord/iso_queue.cpp
        overlord/isocommon.cpp
        overlord/overlord.cpp
        overlord/ramdisk.cpp
        overlord/sbank.cpp
        overlord/soundcommon.cpp
        overlord/srpc.cpp
        overlord/ssound.cpp
        overlord/stream.cpp)

# the runtime should be built without any static/dynamic libraries.
add_executable(gk ${RUNTIME_SOURCE} main.cpp)

# we also build a runtime library for testing. This version is likely unable to call GOAL code correctly, but
# can be used to test other things.
add_library(runtime ${RUNTIME_SOURCE})

IF (WIN32)
  # set stuff for windows
  target_link_libraries(gk cross_sockets mman common_util)
ELSE()
  # set stuff for other systems
  target_link_libraries(gk cross_sockets pthread common_util)
ENDIF()
