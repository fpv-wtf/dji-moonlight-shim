#pragma once

void gfx_init();
void gfx_deinit();

void gfx_splash_show();
void gfx_splash_hide();

void gfx_toast(const char *text);
void gfx_toast_tick();
void gfx_toast_clear();
