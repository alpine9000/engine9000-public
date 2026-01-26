// Shared export helpers for Windows builds
#pragma once

#if defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
#  ifdef __GNUC__
#    define GEO_EXPORT __attribute__((__dllexport__))
#  else
#    define GEO_EXPORT __declspec(dllexport)
#  endif
#else
#  if defined(__GNUC__) && __GNUC__ >= 4
#    define GEO_EXPORT __attribute__((__visibility__("default")))
#  else
#    define GEO_EXPORT
#  endif
#endif
