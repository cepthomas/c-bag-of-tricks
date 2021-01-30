
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

#include "state_machine.h"
#include "list.h"


/// @brief Definition of state machine.


//---------------- Private --------------------------//

/// For debugging if enabled.
#define MAX_TRACE 100

/// One transition in a state.
typedef struct
{
    unsigned int eventId;       ///< Unique id for the trigger event.
    func_t func;                ///< Optional function to execute on entry.
    unsigned int nextStateId;   ///< Next state to go to. Can be ST_SAME.
} transDesc_t;

/// One state and its handled events.
typedef struct
{
    unsigned int stateId;       ///< Unique id for this state.
    func_t func;                ///< Optional function to execute on entry.
    list_t* transDescs;         ///< List of transitions - transDesc_t
} stateDesc_t;

/// Describes the behavior of a state machine instance.
struct sm
{
    // Stuff supplied by client.
    FILE* fp;                   ///< For logging output - optional.
    xlat_t xlat;                ///< Client supplied translation for logging - optional.
    unsigned int defState;      ///< The default state id.
    unsigned int defEvent;      ///< The default event id.
    // Internal context stuff.
    list_t* stateDescs;         ///< All the states - stateDesc_t.
    stateDesc_t* currentState;  ///< The current state.
    stateDesc_t* defaultState;  ///< Maybe a default state.
    list_t* eventQueue;         ///< Queue of events to be processed - int.
    bool processingEvents;      ///< Internal flag for recursion.
};


//---------------- Public API Implementation -------------//


//--------------------------------------------------------//
sm_t* sm_create(FILE* fp, xlat_t xlat, unsigned int defState, unsigned int defEvent)
{
    CREATE_INST(sm, sm_t, BAD_PTR);

    sm->fp = fp;
    sm->xlat = xlat;
    sm->defState = defState;
    sm->defEvent = defEvent;

    sm->stateDescs = list_create();
    sm->currentState = NULL;
    sm->defaultState = NULL;
    sm->eventQueue = list_create();
    sm->processingEvents = false;

    return sm;
}

//--------------------------------------------------------//
int sm_destroy(sm_t* sm)
{
    VAL_PTR(sm, RS_ERR);

    int ret = RS_PASS;

    stateDesc_t* st;

    // Clean up sub-list.
    list_iterStart(sm->stateDescs);
    while(RS_PASS == list_iterNext(sm->stateDescs, (void**)&st))
    {
        list_destroy(st->transDescs);
    }

    list_destroy(sm->stateDescs);
    list_destroy(sm->eventQueue);

    free(sm);

    return ret;
}

//--------------------------------------------------------//
int sm_reset(sm_t* sm, unsigned int stateId)
{
    VAL_PTR(sm, RS_ERR);

    int ret = RS_PASS;

    stateDesc_t* st;

    list_iterStart(sm->stateDescs);
    while(RS_PASS == list_iterNext(sm->stateDescs, (void**)&st))
    {
        VAL_PTR(st, RS_ERR);
        if(st->stateId == stateId) // found it
        {
            sm->currentState = st;

            if(sm->currentState->func != NULL)
            {
                sm->currentState->func();
            }
        }
    }

    return ret;
}

//--------------------------------------------------------//
int sm_getState(sm_t* sm)
{
    VAL_PTR(sm, RS_ERR);

    return sm->currentState->stateId;
}

//--------------------------------------------------------//
int sm_addState(sm_t* sm, unsigned int stateId, const func_t func)
{
    int ret = RS_PASS;

    CREATE_INST(stateDesc, stateDesc_t, RS_ERR);

    stateDesc->stateId = stateId;
    stateDesc->func = func;
    stateDesc->transDescs = list_create();

    list_push(sm->stateDescs, stateDesc);
    sm->currentState = stateDesc; // for adding transitions

    if(stateId == sm->defState) // keep a reference to this one
    {
        sm->defaultState = stateDesc;
    }

    return ret;
}

//--------------------------------------------------------//
int sm_addTransition(sm_t* sm, unsigned int eventId, const func_t func, unsigned int nextState)
{
    int ret = RS_PASS;

    CREATE_INST(transDesc, transDesc_t, RS_ERR);

    transDesc->eventId = eventId;
    transDesc->func = func;
    transDesc->nextStateId = nextState;

    list_push(sm->currentState->transDescs, transDesc);

    return ret;
}

//--------------------------------------------------------//
int sm_processEvent(sm_t* sm, unsigned int eventId)
{
    VAL_PTR(sm, RS_ERR);

    int ret = RS_PASS;

    // Transition functions may generate new events so keep a queue.
    // This allows current execution to complete before handling new event.
    CREATE_INST(ld, unsigned int, RS_ERR);
    *ld = eventId;
    list_push(sm->eventQueue, ld);

    // Check for recursion through the processing loop - event may be generated internally during processing.
    if (!sm->processingEvents)
    {
        sm->processingEvents = true;

        // Process all events in the event queue.
        int* qevt;
        while (RS_PASS == list_pop(sm->eventQueue, (void**)&qevt))
        {
            VAL_PTR(qevt, RS_ERR);
            unsigned int qevtid = *qevt;
            free(qevt);

            sm_trace(sm, __LINE__, "Process current state %s event %s\n",
                     sm->xlat(sm->currentState->stateId), sm->xlat(qevtid));

            // Find match with this event for present state.
            transDesc_t* transDesc = NULL;
            transDesc_t* defDesc = NULL;

            // Try default state first.
            if(sm->defaultState != NULL)
            {
                transDesc_t* trans = NULL;
                list_iterStart(sm->defaultState->transDescs);
                while(RS_PASS == list_iterNext(sm->defaultState->transDescs, (void**)&trans))
                {
                    if(trans->eventId == qevtid) // found it
                    {
                        transDesc = trans;
                    }
                }
            }

            // Otherwise check the regulars.
            if(transDesc == NULL)
            {
                transDesc_t* trans = NULL;
                list_iterStart(sm->currentState->transDescs);
                while(RS_PASS == list_iterNext(sm->currentState->transDescs, (void**)&trans))
                {
                    if(trans->eventId == qevtid) // found it
                    {
                        transDesc = trans;
                    }
                    else if(trans->eventId == sm->defEvent)
                    {
                        defDesc = trans;
                    }
                }
            }

            if(transDesc == NULL)
            {
                transDesc = defDesc;
            }

            if(transDesc != NULL)
            {
                // Execute the transition function.
                if(transDesc->func != NULL)
                {
                    transDesc->func();
                }

                // Process the next state.
                if(transDesc->nextStateId != sm->currentState->stateId)
                {
                    // State is changing. Find the new state.
                    stateDesc_t* st = NULL;
                    stateDesc_t* nextState = NULL;
                    list_iterStart(sm->stateDescs);

                    while(RS_PASS == list_iterNext(sm->stateDescs, (void**)&st))
                    {
                        if(st->stateId == transDesc->nextStateId) // found it
                        {
                            nextState = st;
                        }
                    }

                    if(nextState != NULL)
                    {
                        sm_trace(sm, __LINE__, "Changing state from %s to %s\n",
                                 sm->xlat(sm->currentState->stateId), sm->xlat(nextState->stateId));
                        sm->currentState = nextState;
                        if(sm->currentState->func != NULL)
                        {
                            sm->currentState->func();
                        }
                    }
                    else
                    {
                        sm_trace(sm, __LINE__, "Couldn't find next state from %s to %s\n",
                                 sm->xlat(sm->currentState->stateId), sm->xlat(nextState->stateId)); // Should be an error.
                    }
                }
                else
                {
                    sm_trace(sm, __LINE__, "Same state %s\n", sm->xlat(sm->currentState->stateId));
                }
            }
            else
            {
                sm_trace(sm, __LINE__, "No match for state %s for event %s\n",
                         sm->xlat(sm->currentState->stateId), sm->xlat(qevtid));
            }
        }
    }
    
    // Done for now.
    sm->processingEvents = false;

    return ret;
}

//--------------------------------------------------------//
int sm_trace(sm_t* sm, int line, const char* format, ...)
{
    VAL_PTR(sm, RS_ERR);
    VAL_PTR(format, RS_ERR);

    int ret = RS_PASS;

    if(sm->fp != NULL)
    {
        va_list args;
        va_start(args, format);

        char sfmt[MAX_TRACE];
        vsnprintf(sfmt, MAX_TRACE-1, format, args);
        va_end(args);

        fprintf(sm->fp, "SM(%d): %s", line, sfmt);
    }

    return ret;
}

//--------------------------------------------------------//
int sm_toDot(sm_t* sm, FILE* fp)
{
    VAL_PTR(sm, RS_ERR);
    VAL_PTR(fp, RS_ERR);

    int ret = RS_PASS;

    // Init attributes for dot.
    fprintf(fp, "digraph StateDiagram {\n");
    fprintf(fp, "    ratio=\"compress\";\n");
    fprintf(fp, "    fontname=\"Arial\";\n");
    fprintf(fp, "    label=\"\";\n"); // (your label here!)
    fprintf(fp, "    node [\n");
    fprintf(fp, "    height=\"1.00\";\n");
    fprintf(fp, "    width=\"1.5\";\n");
    fprintf(fp, "    shape=\"ellipse\";\n");
    fprintf(fp, "    fixedsize=\"true\";\n");
    fprintf(fp, "    fontsize=\"8\";\n");
    fprintf(fp, "    fontname=\"Arial\";\n");
    fprintf(fp, "];\n");
    fprintf(fp, "\n");
    fprintf(fp, "    edge [\n");
    fprintf(fp, "    fontsize=\"8\";\n");
    fprintf(fp, "    fontname=\"Arial\";\n");
    fprintf(fp, "];\n");
    fprintf(fp, "\n");

    // Generate actual nodes and edges from states
    // Iterate all states.
    stateDesc_t* st = NULL;
    transDesc_t* trans = NULL;
    list_iterStart(sm->stateDescs);
    
    while(RS_PASS == list_iterNext(sm->stateDescs, (void**)&st))
    {
        // Iterate through the state transitions.
        list_iterStart(st->transDescs);
        while(RS_PASS == list_iterNext(st->transDescs, (void**)&trans))
        {
            fprintf(fp, "        \"%s\" -> \"%s\" [label=\"%s\"];\n",
                    sm->xlat(st->stateId),
                    trans->nextStateId == st->stateId ? sm->xlat(st->stateId) : sm->xlat(trans->nextStateId),
                    sm->xlat(trans->eventId) );
        }
    }

    fprintf(fp, "}\n");

    return ret;
}