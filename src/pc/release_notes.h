#ifndef VR_RELEASE_NOTES_H
#define VR_RELEASE_NOTES_H
#include <stdbool.h>
#include <stdint.h>
bool vr_release_notes_load(void);
int32_t vr_release_notes_page_count(void);
const char *vr_release_notes_page_get(int32_t page);
#endif
