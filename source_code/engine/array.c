#include "array.h"
#include "memory.h"
#include "interruptions.h"
#include "log.h"
#include <string.h>
#include <engine/macros.h>

int array_init(Array * array, u32 element_bytes_size, int count){
    ZERO(*(array));
    if(array == NULL){
        LOG("Array is NULL");
        return -1;
    }

    if(array->initialized != true){
       
    }else{
        //LOG("Array already initialized\n");
        return -1;
    }  
    array->count = 0;
    array->data = allocate_stack_memory_alignmed(element_bytes_size * count,16);
    array->actual_bytes_size = 0;
    array->element_bytes_size = element_bytes_size;
    array->bytes_capacity = element_bytes_size * count;
    array->initialized = true;
    array->element_capacity = count;
    return 0;
}


int array_new_pointer(Array* array,int count){
  array_init(array,sizeof(void*),count);
}


void array_resize(Array* array, int count){
    array->actual_bytes_size = count * array->element_bytes_size;
    array->count = count;
}

void array_add(Array* array,const void* element){
    if(array->initialized == false){
        LOG("Array not initialized\n");
        debug_break();;
        return;
    }
    if(array->bytes_capacity < (array->actual_bytes_size + array->element_bytes_size)){
        LOG("Element byte size %i\n",array->element_bytes_size);
        LOG("Current bytes size: %i\n",array->actual_bytes_size);
        LOG("Needed %i\n",(array->actual_bytes_size + array->element_bytes_size));
        LOG("Array byte capacity %i\n",array->bytes_capacity);
        LOG("Array need reallocation\n");//TODO: reallocation engine memory
        debug_break();
    }
    if(array->count == 0){
        array->actual_bytes_size += array->element_bytes_size;
        memcpy(array->data,element,array->element_bytes_size);
        array->count++;
        return;
    }
    size_t offset = array->actual_bytes_size;
    array->count++;
    array->actual_bytes_size += array->element_bytes_size;
    
    memcpy(array->data+(offset),element,array->element_bytes_size);
}

void array_add_pointer(Array* array, const void* element){
    array_add(array,&element);
}

void *array_get_last(Array* array){
    if(array->count > 0){
        return array_get(array, array->count-1);
    }else
        return NULL;
}


void *array_get(Array* array,int index){
    if(array->count == 0){
        //LOG("Array is empty\n");
        return NULL;
    }
    if(array->initialized == false){
        LOG("Array not initialized\n");
        debug_break();;
        return NULL;
    }
    if(index > array->count-1){
        LOG("Element out of range, array count: %i , requested %i\n",array->count,index);
//				debug_break();
        return NULL;
    }
    size_t offset = array->element_bytes_size;

   /*  if(array->isPointerToPointer){
        void* data = NULL;
        if(index == 0)
            data = &array->data[0];
        else
            data = &array->data[0] + (index*offset);
        return data;
    } */   

   
    if(index == 0)
        return array->data;
    return array->data + (index*offset);
}

void *array_get_pointer(Array* array,int index){
    void** ppointer = array_get(array,index);
    void* pointer = ppointer[0];
    return pointer;
}

void array_clean(Array* array){
    array->count = 0;    
    array->actual_bytes_size = 0;
}
