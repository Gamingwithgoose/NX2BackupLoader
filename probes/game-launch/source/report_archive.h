/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
/* Preparation only. Returns files copied, or -1 for a partial/failed archive.
 * Originals remain available; report archival must never prevent launching. */
int nx2_archive_previous_reports(void);
