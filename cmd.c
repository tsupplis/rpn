/*
 * cmd.c - command registry.
 *
 * Commands live in static tables owned by the modules that implement them
 * (stack.c, arith.c, ...).  Lookup is a linear scan over every table by
 * canonical name or alias; the tables are small.
 */
#include <string.h>
#include "intern.h"

static const rpn_cmd_t *const tables[] = {
    rpn_stack_cmds,
    rpn_arith_cmds,
    rpn_math_cmds,
    rpn_prog_cmds,
    rpn_var_cmds,
    rpn_cond_cmds,
    NULL
};

const rpn_cmd_t *rpn_cmd_find(const char *name)
{
    const rpn_cmd_t *const *t;
    const rpn_cmd_t *row;

    for (t = tables; *t; t++) {
        for (row = *t; row->name; row++) {
            if (strcmp(row->name, name) == 0) {
                return row;
            }
            if (row->alias && strcmp(row->alias, name) == 0) {
                return row;
            }
        }
    }
    return NULL;
}
