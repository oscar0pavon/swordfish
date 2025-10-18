#include <X11/Xlib.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>


#include "input.h"
#include "window.h"



int main(){
 

  create_window();

  pthread_t thread_id;

  pthread_create(&thread_id,NULL,handle_input, NULL);


  while (swordfish_running) {
  
   //draw cube 
    printf("Compiling..\n");
    sleep(1);

  }

  close_window();  




  return EXIT_SUCCESS;
}
