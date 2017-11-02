/*
 * Copyright (C) 2017 Swift Navigation Inc.
 * Contact: Gareth McMullin <gareth@swiftnav.com>
 *
 * This source is subject to the license found in the file 'LICENSE' which must
 * be be distributed together with this source. All other rights reserved.
 *
 * THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND,
 * EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <libpiksi/logging.h>

#include "ntrip_source.h"

#define FIFO_FILE_PATH "/var/run/ntrip_source"

static bool ntrip_enabled;
static char ntrip_url[256];

typedef struct {
  int (*execfn)(void);
  int pid;
} ntrip_process_t;

static int ntrip_daemon_execfn(void) {
  char *argv[] = {
    "ntrip_source_daemon", "--debug",
    "--file", FIFO_FILE_PATH,
    "--url", ntrip_url,
    NULL,
  };

  return execvp(argv[0], argv);
}

static int ntrip_adapter_execfn(void) {
  char *argv[] = {
    "zmq_adapter", "--debug",
    "--file", FIFO_FILE_PATH,
    "-s", ">tcp://127.0.0.1:43080",
    "--filter-out", "sbp",
    "--filter-out-config", "/etc/skylark_upload_filter_out_config",
    NULL,
  };

  return execvp(argv[0], argv);
}

static ntrip_process_t ntrip_processes[] = {
  { .execfn = ntrip_adapter_execfn },
  { .execfn = ntrip_daemon_execfn },
};

static const int ntrip_processes_count =
  sizeof(ntrip_processes)/sizeof(ntrip_processes[0]);

static int ntrip_notify(void *context)
{
  (void)context;

  for (int i=0; i<ntrip_processes_count; i++) {
    ntrip_process_t *process = &ntrip_processes[i];

    if (process->pid != 0) {
      int ret = kill(process->pid, SIGTERM);
      if (ret != 0) {
        piksi_log(LOG_ERR, "kill pid %d error (%d) \"%s\"",
                  process->pid, errno, strerror(errno));
      }
      sleep(1.0);
      ret = kill(process->pid, SIGKILL);
      if (ret != 0 && errno != ESRCH) {
        piksi_log(LOG_ERR, "force kill pid %d error (%d) \"%s\"",
                  process->pid, errno, strerror(errno));
      }
      process->pid = 0;
    }

    if (!ntrip_enabled || strcmp(ntrip_url, "") == 0) {
      continue;
    }

    process->pid = fork();
    if (process->pid == 0) {
      process->execfn();
      piksi_log(LOG_ERR, "exec error (%d) \"%s\"", errno, strerror(errno));
      exit(EXIT_FAILURE);
    }
  }

  return 0;
}

void ntrip_source_init(settings_ctx_t *settings_ctx)
{
  mkfifo(FIFO_FILE_PATH, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

  settings_register(settings_ctx, "ntrip_source", "enable",
                    &ntrip_enabled, sizeof(ntrip_enabled),
                    SETTINGS_TYPE_BOOL,
                    ntrip_notify, NULL);

  settings_register(settings_ctx, "ntrip_source", "url",
                    &ntrip_url, sizeof(ntrip_url),
                    SETTINGS_TYPE_STRING,
                    ntrip_notify, NULL);
}