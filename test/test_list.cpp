#include <cstdio>
#include <cstring>

#include "common.h"

#include "pnut.h"

extern "C"
{
#include "list.h"
}

typedef struct
{
    int anumber;
    const char* astring;
} test_struct_t;



/////////////////////////////////////////////////////////////////////////////
UT_SUITE(LIST_ALL, "Test all list functions.")
{
    int i;

    test_struct_t st1 { .anumber = 11, .astring = "Ajay1" };
    test_struct_t st2 { .anumber = 22, .astring = "Ajay2" };
    test_struct_t st3 { .anumber = 33, .astring = "Ajay3" };
    test_struct_t st4 { .anumber = 44, .astring = "Ajay4" };
    test_struct_t st5 { .anumber = 55, .astring = "Ajay5" };

    // Make a list.
    list_t* mylist = list_create();

    // Add a node at the beginning.
    list_push(mylist, &st1);

    // Add a node at the beginning.
    list_push(mylist, &st2);

    // Add a node at the end.
    list_append(mylist, &st3);

    // Add a node at the beginning.
    list_push(mylist, &st4);

    UT_EQUAL(list_count(mylist), 4);

    // Iterate through list.
    i = 0;
    test_struct_t* data;
    list_start(mylist);
    while(list_next(mylist, (void**)&data))
    {
        UT_NOT_NULL(data);

        switch(i)
        {
            case 0:
                UT_EQUAL(data->anumber, 44);
                UT_STR_EQUAL(data->astring, "Ajay4");
                break;

            case 1:
                UT_EQUAL(data->anumber, 22);
                UT_STR_EQUAL(data->astring, "Ajay2");
                break;

            case 2:
                UT_EQUAL(data->anumber, 11);
                UT_STR_EQUAL(data->astring, "Ajay1");
                break;

            case 3:
                UT_EQUAL(data->anumber, 33);
                UT_STR_EQUAL(data->astring, "Ajay3");
                break;
        }
        i++;
    }

    // Test pop.
    bool ok = list_pop(mylist, (void**)&data);
    UT_TRUE(ok);
    UT_EQUAL(list_count(mylist), 3);
    UT_NOT_NULL(data);
    UT_EQUAL(data->anumber, 33);
    UT_STR_EQUAL(data->astring, "Ajay3");
    // I own this now so clean up.
    free(data);

    // Add another.
    list_push(mylist, &st5);

    // Test pop.
    ok = list_pop(mylist, (void**)&data);
    UT_TRUE(ok);
    UT_EQUAL(list_count(mylist), 3);
    UT_NOT_NULL(data);
    UT_EQUAL(data->anumber, 11);
    UT_STR_EQUAL(data->astring, "Ajay1");
    // I own this now so clean up.
    free(data);

    // Remove everything.
    list_clear(mylist);
    UT_NOT_NULL(mylist);
    UT_EQUAL(list_count(mylist), 0);

    list_destroy(mylist);
}
