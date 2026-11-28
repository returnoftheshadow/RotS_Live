
#pragma once

#if defined(__linux__) || defined(unix) || defined(__unix) || defined(__unix__) || defined(__FreeBSD__)
#define PREDEF_PLATFORM_LINUX
#endif

#if defined(_WIN32)
#define PREDEF_PLATFORM_WINDOWS
#endif

#if defined PREDEF_PLATFORM_LINUX

#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/param.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#endif

#if defined PREDEF_PLATFORM_WINDOWS

// Winsock: the socket API rots_net.h wraps on Windows. winsock2.h must come before any
// <windows.h>, whose older winsock.h conflicts with it.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
// <windows.h> otherwise defines min and max as macros, which break every std::min/std::max.
#ifndef NOMINMAX
#define NOMINMAX
#endif
// Sizes fd_set, so it must precede winsock2.h. Winsock's default of 64 is below the 256
// connections the select() loop accepts (MAX_DESCRIPTORS_AVAILABLE in comm.cpp), and FD_SET
// silently drops sockets past the limit.
#ifndef FD_SETSIZE
#define FD_SETSIZE 256
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

using SocketType = SOCKET;

#else

using SocketType = int;

#endif

using sh_int = signed short int;
using ush_int = unsigned short int;

using byte = unsigned char;
using sbyte = signed char;
using ubyte = unsigned char;

constexpr auto COPY_COMMAND = "cp";
