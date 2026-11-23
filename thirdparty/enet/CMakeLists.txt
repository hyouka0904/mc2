# ENet 1.3.18 (MIT, see LICENSE): reliable UDP for LAN multiplayer.
# Vendored sources from https://github.com/lsalzman/enet (tag v1.3.18);
# only the library sources, headers, LICENSE and README are kept.

include(CheckFunctionExists)
include(CheckStructHasMember)
include(CheckTypeSize)

add_library(enet STATIC
    callbacks.c compress.c host.c list.c packet.c peer.c protocol.c
    unix.c win32.c)
target_include_directories(enet PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
set_target_properties(enet PROPERTIES C_STANDARD 99 POSITION_INDEPENDENT_CODE ON)

if(WIN32)
    target_link_libraries(enet PUBLIC ws2_32 winmm iphlpapi) # iphlpapi: gos_net interface list
    if(MSVC)
        target_compile_options(enet PRIVATE /W0)
    endif()
else()
    foreach(fn fcntl poll getaddrinfo getnameinfo gethostbyname_r gethostbyaddr_r inet_pton inet_ntop)
        string(TOUPPER ${fn} FN)
        check_function_exists(${fn} HAS_${FN})
        if(HAS_${FN})
            target_compile_definitions(enet PRIVATE HAS_${FN}=1)
        endif()
    endforeach()
    check_struct_has_member("struct msghdr" "msg_flags" "sys/types.h;sys/socket.h" HAS_MSGHDR_FLAGS)
    if(HAS_MSGHDR_FLAGS)
        target_compile_definitions(enet PRIVATE HAS_MSGHDR_FLAGS=1)
    endif()
    set(CMAKE_EXTRA_INCLUDE_FILES "sys/types.h" "sys/socket.h")
    check_type_size("socklen_t" HAS_SOCKLEN_T BUILTIN_TYPES_ONLY)
    unset(CMAKE_EXTRA_INCLUDE_FILES)
    if(HAS_SOCKLEN_T)
        target_compile_definitions(enet PRIVATE HAS_SOCKLEN_T=1)
    endif()
    target_compile_options(enet PRIVATE -w)
endif()

add_library(MC2::ENet ALIAS enet)
