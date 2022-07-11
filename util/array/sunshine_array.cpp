/**
 * @file sunshine_array.cpp
 * @author {Do Huy Hoang} ({huyhoangdo0205@gmail.com})
 * @brief 
 * @version 1.0
 * @date 2022-07-07
 * 
 * @copyright Copyright (c) 2022
 * 
 */
#include <sunshine_array.h>
#include <sunshine_macro.h>
#include <string.h>



namespace util
{
    /**
     * @brief 
     * 
     */
    struct _ListObject{
        ObjectContainer* first;

        uint length;
    };      


    ListObject*
    list_object_new()
    {
        ListObject* arr = (ListObject*)malloc(sizeof(ListObject));
        memset(arr,0,sizeof(ListObject));

        arr->length=0;
        return arr;
    }

    void
    array_object_finalize(ListObject* arr)
    {
        ObjectContainer* container = arr->first;
        while (!container->next) 
        { 
            OBJECT_CLASS->unref(container->obj);
            container = container->next; 
        }
        free(arr);
    }

    void
    array_object_emplace_back(ListObject* array,
                              Object* obj)
    {
        ObjectContainer* container = NULL;
        if (array->length)
        {
            container = array->first;
            while (!container->next) { container = container->next; }
        }
        
        ObjectContainer* last = (ObjectContainer*)malloc(sizeof(ObjectContainer));
        memset(last,0,sizeof(ObjectContainer));

        last->obj  = obj;
        last->next = NULL;

        if (array->length)
            container->next = last;
        else
            array->first = last;

        array->length++;
    }

    bool
    array_object_has_data(ListObject* array,
                          int index)
    {
        return index < array->length;
    }

    Object* 
    array_object_get_data(ListObject* array,
                          int index)
    {
        if (!array_object_has_data(array,index))
            return NULL;
        
        int count = 0;
        ObjectContainer* prev_container,*container,*next_container;
        container = array->first;
        while (!container->next || count == index) 
        { 
            prev_container = container;
            container = container->next; 

            if (container->next)
                next_container = container->next;
        }

        Object* ret = container->obj;

        if(next_container)
            prev_container->next = next_container;

        return ret;
    }


    int             
    array_object_length(ListObject* array)
    {
        return array->length;
    }
    
    ListObjectClass* 
    list_object_class_init()
    {
        static bool initialize = false;
        static ListObjectClass klass = {0};
        if (initialize)
            return &klass;
        
        initialize = true;
        klass.emplace_back = array_object_emplace_back;
        klass.finalize     = array_object_finalize;
        klass.has_data     = array_object_has_data;
        klass.get_data     = array_object_get_data;
        klass.init         = list_object_new;
        klass.length       = array_object_length;
        return &klass;
    }
} // namespace util
