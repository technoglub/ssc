/*
Copyright (c) 2019 Stuart Steven Calder
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and
the following disclaimer in the documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#pragma once

/* Symbol visibility macros */
#if defined( BUILD_STATIC ) || defined( IMPORT_STATIC )
#   define DLL_PUBLIC
#   define DLL_LOCAL
#else
#   if defined( _WIN32 ) || defined( __CYGWIN__ )
#       if defined( BUILD_DLL )
#           if defined( __GNUC__ )
#               define DLL_PUBLIC __attribute__ ((dllexport))
#           else
#               define DLL_PUBLIC __declspec(dllexport)
#           endif
#       else
#           if defined( __GNUC__ )
#               define DLL_PUBLIC __attribute__ ((dllimport))
#           else
#               define DLL_PUBLIC __declspec(dllimport)
#           endif
#       endif
#       define DLL_LOCAL
#   else
#       if defined( __GNUC__ ) && ( __GNUC__ >= 4 )
#           define DLL_PUBLIC __attribute__ ((visibility ("default")))
#           define DLL_LOCAL  __attribute__ ((visibility ("hidden")))
#       else
#           define DLL_PUBLIC
#           define DLL_LOCAL
#       endif
#   endif
#endif

/* Common OS check macros */
#if defined(__OpenBSD__) || defined(__gnu_linux__)
#	define __Unix_Like__ 1
#endif /* defined(__OpenBSD__) || defined(__gnu_linux__) */
