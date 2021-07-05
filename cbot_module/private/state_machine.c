
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

#include "state_machine.h"
#include "list.h"


/// @brief Definition of state machine.


//---------------- Private Declarations ------------------//

/// For debugging if enabled.
static const int MAX_TRACE = 100;

/// One transition in a state.
typedef struct
{
    unsigned int event_id;      ///< Unique id for the trigger event.
    func_t func;                ///< Optional function to execute on entry.
    unsigned int next_state_id; ///< Next state to go to. Can be ST_SAME.
} trans_desc_t;

/// One state and its handled events.
typedef struct
{
    unsigned int state_id;      ///< Unique id for this state.
    func_t func;                ///< Optional function to execute on entry.
    list_t* trans_descs;        ///< List of transitions - trans_desc_t
} state_desc_t;

/// Describes the behavior of a state machine instance.
struct sm
{
    // Stuff supplied by client.
    FILE* fp;                   ///< For logging output - optional.
    xlat_t xlat;                ///< Client supplied translation for logging - optional.
    unsigned int def_state;     ///< The default state id.
    unsigned int def_event;     ///< The default event id.
    // Internal context stuff.
    list_t* state_descs;        ///< All the states - state_desc_t.
    state_desc_t* current_state;///< The current state.
    state_desc_t* default_state;///< Maybe a default state.
    list_t* event_queue;        ///< Queue of events to be processed - int.
    bool processing_events;     ///< Internal flag for recursion.
};


//---------------- Public API Implementation -------------//


//--------------------------------------------------------//
sm_t* sm_Create(FILE* fp, xlat_t xlat, unsigned int def_state, unsigned int def_event)
{
    CREATE_INST(sm, sm_t, BAD_PTR);

    sm->fp = fp;
    sm->xlat = xlat;
    sm->def_state = def_state;
    sm->def_event = def_event;

    sm->state_descs = list_Create();
    sm->current_state = NULL;
    sm->default_state = NULL;
    sm->event_queue = list_Create();
    sm->processing_events = false;

    return sm;
}

//--------------------------------------------------------//
int sm_Destroy(sm_t* sm)
{
    VAL_PTR(sm, RS_ERR);

    int ret = RS_PASS;

    state_desc_t* st;

    // Clean up sub-list.
    list_IterStart(sm->state_descs);
    while(RS_PASS == list_IterNext(sm->state_descs, (void**)&st))
    {
        list_Destroy(st->trans_descs);
    }

    list_Destroy(sm->state_descs);
    list_Destroy(sm->event_queue);

    FREE(sm);

    return ret;
}

//--------------------------------------------------------//
int sm_Reset(sm_t* sm, unsigned int state_id)
{
    VAL_PTR(sm, RS_ERR);

    int ret = RS_PASS;

    state_desc_t* st;

    list_IterStart(sm->state_descs);
    while(RS_PASS == list_IterNext(sm->state_descs, (void**)&st))
    {
        VAL_PTR(st, RS_ERR);
        if(st->state_id == state_id) // found it
        {
            sm->current_state = st;

            if(sm->current_state->func != NULL)
            {
                sm->current_state->func();
            }
        }
    }

    return ret;
}

//--------------------------------------------------------//
int sm_GetState(sm_t* sm)
{
    VAL_PTR(sm, RS_ERR);

    return sm->current_state->state_id;
}

//--------------------------------------------------------//
int sm_AddState(sm_t* sm, unsigned int state_id, const func_t func)
{
    int ret = RS_PASS;

    CREATE_INST(state_desc, state_desc_t, RS_ERR);

    state_desc->state_id = state_id;
    state_desc->func = func;
    state_desc->trans_descs = list_Create();

    list_Push(sm->state_descs, state_desc);
    sm->current_state = state_desc; // for adding transitions

    if(state_id == sm->def_state) // keep a reference to this one
    {
        sm->default_state = state_desc;
    }

    return ret;
}

//--------------------------------------------------------//
int sm_AddTransition(sm_t* sm, unsigned int event_id, const func_t func, unsigned int next_state)
{
    int ret = RS_PASS;

    CREATE_INST(trans_desc, trans_desc_t, RS_ERR);

    trans_desc->event_id = event_id;
    trans_desc->func = func;
    trans_desc->next_state_id = next_state;

    list_Push(sm->current_state->trans_descs, trans_desc);

    return ret;
}

//--------------------------------------------------------//
int sm_ProcessEvent(sm_t* sm, unsigned int event_id)
{
    VAL_PTR(sm, RS_ERR);

    int ret = RS_PASS;

    // Transition functions may generate new events so keep a queue.
    // This allows current execution to complete before handling new event.
    CREATE_INST(ld, unsigned int, RS_ERR);
    *ld = event_id;
    list_Push(sm->event_queue, ld);

    // Check for recursion through the processing loop - event may be generated internally during processing.
    if (!sm->processing_events)
    {
        sm->processing_events = true;

        // Process all events in the event queue.
        int* qevt;
        while (RS_PASS == list_Pop(sm->event_queue, (void**)&qevt))
        {
            VAL_PTR(qevt, RS_ERR);
            unsigned int qevtid = *qevt;
            FREE(qevt);

            sm_Trace(sm, __LINE__, "Process current state %s event %s\n",
                     sm->xlat(sm->current_state->state_id), sm->xlat(qevtid));

            // Find match with this event for present state.
            trans_desc_t* trans_desc = NULL;
            trans_desc_t* def_desc = NULL;

            // Try default state first.
            if(sm->default_state != NULL)
            {
                trans_desc_t* trans = NULL;
                list_IterStart(sm->default_state->trans_descs);
                while(RS_PASS == list_IterNext(sm->default_state->trans_descs, (void**)&trans))
                {
                    if(trans->event_id == qevtid) // found it
                    {
                        trans_desc = trans;
                    }
                }
            }

            // Otherwise check the regulars.
            if(trans_desc == NULL)
            {
                trans_desc_t* trans = NULL;
                list_IterStart(sm->current_state->trans_descs);
                while(RS_PASS == list_IterNext(sm->current_state->trans_descs, (void**)&trans))
                {
                    if(trans->event_id == qevtid) // found it
                    {
                        trans_desc = trans;
                    }
                    else if(trans->event_id == sm->def_event)
                    {
                        def_desc = trans;
                    }
                }
            }

            if(trans_desc == NULL)
            {
                trans_desc = def_desc;
            }

            if(trans_desc != NULL)
            {
                // Execute the transition function.
                if(trans_desc->func != NULL)
                {
                    trans_desc->func();
                }

                // Process the next state.
                if(trans_desc->next_state_id != sm->current_state->state_id)
                {
                    // State is changing. Find the new state.
                    state_desc_t* st = NULL;
                    state_desc_t* next_state = NULL;
                    list_IterStart(sm->state_descs);

                    while(RS_PASS == list_IterNext(sm->state_descs, (void**)&st))
                    {
                        if(st->state_id == trans_desc->next_state_id) // found it
                        {
                            next_state = st;
                        }
                    }

                    if(next_state != NULL)
                    {
                        sm_Trace(sm, __LINE__, "Changing state from %s to %s\n",
                                 sm->xlat(sm->current_state->state_id), sm->xlat(next_state->state_id));
                        sm->current_state = next_state;
                        if(sm->current_state->func != NULL)
                        {
                            sm->current_state->func();
                        }
                    }
                    else
                    {
                        sm_Trace(sm, __LINE__, "Couldn't find next state from %s to %s\n",
                                 sm->xlat(sm->current_state->state_id), sm->xlat(next_state->state_id)); // Should be an error.
                    }
                }
                else
                {
                    sm_Trace(sm, __LINE__, "Same state %s\n", sm->xlat(sm->current_state->state_id));
                }
            }
            else
            {
                sm_Trace(sm, __LINE__, "No match for state %s for event %s\n",
                         sm->xlat(sm->current_state->state_id), sm->xlat(qevtid));
            }
        }
    }
    
    // Done for now.
    sm->processing_events = false;

    return ret;
}

//--------------------------------------------------------//
int sm_Trace(sm_t* sm, int line, const char* format, ...)
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
int sm_ToDot(sm_t* sm, FILE* fp)
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
    state_desc_t* st = NULL;
    trans_desc_t* trans = NULL;
    list_IterStart(sm->state_descs);
    
    while(RS_PASS == list_IterNext(sm->state_descs, (void**)&st))
    {
        // Iterate through the state transitions.
        list_IterStart(st->trans_descs);
        while(RS_PASS == list_IterNext(st->trans_descs, (void**)&trans))
        {
            fprintf(fp, "        \"%s\" -> \"%s\" [label=\"%s\"];\n",
                    sm->xlat(st->state_id),
                    trans->next_state_id == st->state_id ? sm->xlat(st->state_id) : sm->xlat(trans->next_state_id),
                    sm->xlat(trans->event_id) );
        }
    }

    fprintf(fp, "}\n");

    return ret;
}
