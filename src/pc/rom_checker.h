#include "types.h"

extern bool gRomIsValid;
extern bool gRomSetupActive;
extern char gRomFilename[];

void legacy_folder_handler(void);

bool main_rom_handler(void);
void rom_on_drop_file(const char *path);
