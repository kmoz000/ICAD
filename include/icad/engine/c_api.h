#pragma once

#include <stddef.h>

#if defined(_WIN32)
#if defined(ICAD_ENGINE_BUILD)
#define ICAD_ENGINE_API __declspec(dllexport)
#else
#define ICAD_ENGINE_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define ICAD_ENGINE_API __attribute__((visibility("default")))
#else
#define ICAD_ENGINE_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct icad_engine_session icad_engine_session;

// Returned strings are UTF-8 and owned by the caller. Release every non-null
// result with icad_engine_string_free.
ICAD_ENGINE_API icad_engine_session* icad_engine_session_create(const char* source_path);
ICAD_ENGINE_API void icad_engine_session_destroy(icad_engine_session* session);
ICAD_ENGINE_API int icad_engine_session_ready(const icad_engine_session* session);
ICAD_ENGINE_API char* icad_engine_session_error(const icad_engine_session* session);
ICAD_ENGINE_API char* icad_engine_session_source(const icad_engine_session* session);
ICAD_ENGINE_API char* icad_engine_session_source_path(const icad_engine_session* session);
ICAD_ENGINE_API char* icad_engine_session_default_export_directory(
    const icad_engine_session* session);
ICAD_ENGINE_API char* icad_engine_session_preview_json(icad_engine_session* session,
                                                       const char* source,
                                                       size_t source_size);
ICAD_ENGINE_API char* icad_engine_session_save_json(icad_engine_session* session,
                                                    const char* source,
                                                    size_t source_size);
ICAD_ENGINE_API char* icad_engine_session_export_json(icad_engine_session* session,
                                                      const char* source,
                                                      size_t source_size,
                                                      const char* output_directory);
ICAD_ENGINE_API void icad_engine_string_free(char* value);

#ifdef __cplusplus
}
#endif
