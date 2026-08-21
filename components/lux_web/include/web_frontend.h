#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Frontend HTML template getters
const char* web_frontend_get_index(void);
const char* web_frontend_get_config(void);
const char* web_frontend_get_setup(void);
const char* web_frontend_get_firmware(void);

#ifdef __cplusplus
}
#endif
