#include "types.h"

extern bool gRomIsValid;
extern bool gRomSetupActive;
extern char gRomFilename[];

void legacy_folder_handler(void);

bool main_rom_handler(void);
void rom_on_drop_file(const char *path);
void rom_browser_begin(void);
void rom_browser_end(void);
void rom_browser_update(void);
void rom_browser_action(int action); // -1/up, 1/down, 2/open, 3/back, 4/locations
const char *rom_browser_text(void);
