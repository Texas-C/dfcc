#include <glib.h>

#include <macro.h>

#include "../config/config.h"
#include "../spawn/subprocess.h"
#include "../version.h"
#include "local.h"


int Client_run_locally (struct Config *config) {
  g_log(DFCC_NAME, G_LOG_LEVEL_DEBUG, "Run locally");

  GError *error = NULL;
  int ret =
    Subprocess_init(NULL, config->cc_argv, NULL, config->prgpath, &error);
  should (error == NULL) otherwise {
    if (error->domain != G_SPAWN_EXIT_ERROR) {
      g_log(DFCC_NAME, G_LOG_LEVEL_CRITICAL, error->message);
    }
    g_error_free(error);
  }
  return ret;
}