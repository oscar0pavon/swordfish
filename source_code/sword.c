#include "sword.h"
#include "cursor.h"

#include <engine/engine2d.h>

void clean_sword(){

  cursor_clean(&cursor);

}

void sword_init(){

  //fills in the ortho projection cursor_init() copies, so it comes first
  pe_2d_init();

  cursor_init(&cursor);

}
