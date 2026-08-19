#pragma once

#include "config_engine.h"
#include "boards.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Schema initialization (legacy compatibility — no longer needed with offsetof,
   but kept for API stability) */
esp_err_t config_schema_init(void);

/* Get board template text (for config_reset_to_template) */
const char* config_get_board_template_text(board_type_t board);

#ifdef __cplusplus
}
#endif