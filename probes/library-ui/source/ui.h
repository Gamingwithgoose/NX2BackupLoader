/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include "folder.h"
#include <switch.h>
/* Returns 1 after a handoff request, 2 to refresh, 0 to exit, -1 on UI failure. */
int nx2_library_ui(Nx2Catalog *,ConsoleFont fallback);
