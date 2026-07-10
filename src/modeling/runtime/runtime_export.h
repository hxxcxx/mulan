#pragma once

#ifdef BUILDING_RUNTIME
#define RUNTIME_API __declspec(dllexport)
#elif defined(BUILDING_RUNTIME_DLL)
#define RUNTIME_API __declspec(dllimport)
#else
#define RUNTIME_API
#endif
