#include "pc/release_notes.h"

#include <stdio.h>
#include <string.h>

#include "pc/platform.h"

#define VR_RELEASE_NOTES_MAX_BYTES (64 * 1024)
#define VR_RELEASE_NOTES_MAX_PAGES 64
#define VR_RELEASE_NOTES_PAGE_BYTES 4096
#define VR_RELEASE_NOTES_LINES_PER_PAGE 12

static char sPages[VR_RELEASE_NOTES_MAX_PAGES][VR_RELEASE_NOTES_PAGE_BYTES];
static int32_t sPageCount;

static void release_notes_add_line(const char* line, size_t length, int32_t* lineCount) {
    if (sPageCount <= 0) {
        sPageCount = 1;
    }
    if (*lineCount >= VR_RELEASE_NOTES_LINES_PER_PAGE ||
        strlen(sPages[sPageCount - 1]) + length + 2 >= VR_RELEASE_NOTES_PAGE_BYTES) {
        if (sPageCount < VR_RELEASE_NOTES_MAX_PAGES) {
            sPageCount++;
            *lineCount = 0;
        }
    }
    if (sPageCount > VR_RELEASE_NOTES_MAX_PAGES) {
        return;
    }
    char* page = sPages[sPageCount - 1];
    size_t used = strlen(page);
    size_t available = VR_RELEASE_NOTES_PAGE_BYTES - used - 1;
    if (length > available) {
        length = available;
    }
    memcpy(page + used, line, length);
    used += length;
    if (used + 1 < VR_RELEASE_NOTES_PAGE_BYTES) {
        page[used++] = '\n';
    }
    page[used] = '\0';
    (*lineCount)++;
}

bool vr_release_notes_load(void) {
    memset(sPages, 0, sizeof(sPages));
    sPageCount = 0;

    char path[SYS_MAX_PATH];
    snprintf(path, sizeof(path), "%s/release_notes.txt", sys_resource_path());
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        snprintf(sPages[0], VR_RELEASE_NOTES_PAGE_BYTES,
                 "Release notes are unavailable.\nPlease check the packaged release files.");
        sPageCount = 1;
        return false;
    }

    char line[VR_RELEASE_NOTES_PAGE_BYTES];
    int32_t lineCount = 0;
    while (fgets(line, sizeof(line), file) != NULL) {
        size_t length = strcspn(line, "\r\n");
        release_notes_add_line(line, length, &lineCount);
        if (sPageCount >= VR_RELEASE_NOTES_MAX_PAGES) {
            break;
        }
    }
    fclose(file);

    if (sPageCount == 0) {
        snprintf(sPages[0], VR_RELEASE_NOTES_PAGE_BYTES, "No release notes are available.");
        sPageCount = 1;
        return false;
    }
    return true;
}

int32_t vr_release_notes_page_count(void) {
    return sPageCount > 0 ? sPageCount : 1;
}

const char* vr_release_notes_page_get(int32_t page) {
    if (page < 0 || page >= vr_release_notes_page_count()) {
        return "";
    }
    return sPages[page];
}
