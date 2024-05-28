#include "incbin.h"
#include "errnes.h"

INCBIN(errnes, "./err.nes");

static void initErrorROM(void){
	errnes_set_err(UNKNOWN_ERR);
	errnes_set_line_num(__LINE__);
	errnes_set_file_name(0);
}
