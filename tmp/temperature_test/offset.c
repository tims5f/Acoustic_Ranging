#include <stddef.h>
#include <stdio.h>
#include "app_ui.h"
int main(void) { printf("temp=%zu speed=%zu auto=%zu ready=%zu distance=%zu size=%zu\n",offsetof(AppUiModel,temperature_deci_c),offsetof(AppUiModel,sound_speed_mm_s),offsetof(AppUiModel,temperature_automatic),offsetof(AppUiModel,temperature_ready),offsetof(AppUiModel,distance_mm),sizeof(AppUiModel)); }
