/*
 * AKENO STREAM PS5 - libSceWebBrowserDialog link stub.
 * Copyright (C) 2026 AKENO STREAM contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Link-time declarations for the system's libSceWebBrowserDialog (the PS5
 * system browser as a common dialog). These bodies are never packaged or
 * executed: the native module writer turns each referenced name into an
 * import of the system module. Keep this list to the functions
 * src/platform/ps5/web_view.cpp calls - all of them were called on hardware
 * (firmware 12.70) by EVO-PLAYER-PS5. A name the firmware's module lacks
 * would make the loader reject the whole title.
 */

int sceWebBrowserDialogInitialize(void)
{
    return -1;
}

int sceWebBrowserDialogOpen(void *param)
{
    (void)param;
    return -1;
}

int sceWebBrowserDialogUpdateStatus(void)
{
    return -1;
}

int sceWebBrowserDialogGetResult(void *result)
{
    (void)result;
    return -1;
}

int sceWebBrowserDialogClose(void)
{
    return -1;
}
