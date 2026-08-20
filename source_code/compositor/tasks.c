#include "tasks.h"
#include "input.h"

//a surface and the seat are separate objects, so the Task has to be matched up
//with whichever TaskInput belongs to the same client before it can be sent a
//key
void focus_task(Task *task) {

  if(!task)
    return;

  if(!task->input){
    TaskInput *temp_input;
    wl_list_for_each(temp_input, &compositor.tasks_input, link) {
      if (temp_input->client == wl_resource_get_client(task->resource))
        task->input = temp_input;
    }
  }

  //cheap when nothing changed, and it is what finally sends wl_keyboard.enter
  //once the client has got round to asking for a keyboard
  set_keyboard_focus(task);
}
