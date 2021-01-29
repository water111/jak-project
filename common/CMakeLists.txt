IF(LINUX)
	add_library(common
        SHARED
        cross_os_debug/xdbg.cpp
        cross_sockets/xsocket.cpp
        goos/Interpreter.cpp
        goos/Object.cpp
        goos/ParseHelpers.cpp
        goos/PrettyPrinter.cpp
        goos/Reader.cpp
        goos/TextDB.cpp
        log/log.cpp
        type_system/deftype.cpp
        type_system/Type.cpp
        type_system/TypeFieldLookup.cpp
        type_system/TypeSpec.cpp
        type_system/TypeSystem.cpp
        util/DgoWriter.cpp
        util/FileUtil.cpp
        util/Timer.cpp
        )
ELSEIF(WIN32)
	add_library(common
        SHARED
        cross_os_debug/xdbg.cpp
        cross_sockets/xsocket.cpp
        goos/Interpreter.cpp
        goos/Object.cpp
        goos/ParseHelpers.cpp
        goos/PrettyPrinter.cpp
        goos/Reader.cpp
        goos/TextDB.cpp
        log/log.cpp
        type_system/deftype.cpp
        type_system/Type.cpp
        type_system/TypeFieldLookup.cpp
        type_system/TypeSpec.cpp
        type_system/TypeSystem.cpp
        util/dgo_util.cpp
        util/DgoReader.cpp
        util/DgoWriter.cpp
        util/FileUtil.cpp
        util/Timer.cpp
        )
ELSEIF(UNIX)
add_library(common
        SHARED
        goos/Interpreter.cpp
        goos/Object.cpp
        goos/ParseHelpers.cpp
        goos/PrettyPrinter.cpp
        goos/Reader.cpp
        goos/TextDB.cpp
        log/log.cpp
        type_system/deftype.cpp
        type_system/Type.cpp
        type_system/TypeFieldLookup.cpp
        type_system/TypeSpec.cpp
        type_system/TypeSystem.cpp
        util/DgoWriter.cpp
        util/FileUtil.cpp
        util/Timer.cpp
        )
ENDIF()

target_link_libraries(common fmt)

IF(WIN32)
    target_link_libraries(common wsock32 ws2_32)
ELSEIF (LINUX)
    target_link_libraries(common stdc++fs)
ELSEIF (UNIX)
    target_link_libraries(common ${Boost_LIBRARIES})
ENDIF()