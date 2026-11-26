/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include "folder.h"
/* Independent bounded reads; never mounts a NUP or keeps a file open. */
void nx2_ui_media(const Nx2Game *,char title[256],unsigned char **image,size_t *bytes);
void nx2_ui_media_exit(void);
