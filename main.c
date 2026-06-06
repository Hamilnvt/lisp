/* TODO
    - quando si vuole entrare in terminal mode si puo' specificare un file per caricare le variabili e le funzioni (ammesso che questo abbia senso)
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dynamic_arrays.h"

#define NOB_IMPLEMENTATION
#include "nob.h"

#define DEBUG

typedef struct LispObject LispObject;

typedef struct {
    LispObject **items;
    size_t count;
    size_t capacity;
} LispObjects;

typedef enum {
    TYPE_FUNCTION,
    TYPE_INTEGER,
    TYPE_BOOLEAN,
    TYPE_STRING,
    __types_count
} LispType ;

struct LispObject {
    bool is_list;
    union {
        struct {
            LispType type;
            union {
                int integer;
                bool boolean;
                char *string;
                char *function;
            } value;
        } atom;
        LispObjects list;
    };
};

LispObject *create_object(void)
{
    LispObject *o = calloc(1, sizeof(LispObject));
    return o;
}

LispObject *create_atom(LispType type)
{
    LispObject *o = create_object();
    o->atom.type = type;
    return o;
}

LispObject *create_list()
{
    LispObject *o = calloc(1, sizeof(LispObject));
    o->is_list = true;
    return o;
}

void add_to_list(LispObject *list, LispObject *item)
{
    assert(list);    
    assert(list->is_list);
    assert(item);

    da_push(&list->list, item);
}

LispObject *create_nil() { return create_list(); }

LispObject *parse_atom(char *atom_str, size_t *_pos)
{
    size_t pos = *_pos;
    LispObject *atom = create_object();
    Nob_String_Builder sb = {0};

    if (isdigit(atom_str[pos])) { // TYPE_INTEGER
        while (isdigit(atom_str[pos])) {
            sb_append(&sb, atom_str[pos]);
            pos++;
        }
        if (!isspace(atom_str[pos]) && atom_str[pos] != '(' && atom_str[pos] != ')') {
            printf("ERROR: number has trailing characters starting with '%c'\n", atom_str[pos]);
            exit(1);
        }
        sb_append_null(&sb);
        atom->atom.type = TYPE_INTEGER;
        atom->atom.value.integer = atoi(sb.items);
    } else if (atom_str[pos] == '"') { // TYPE_STRING
        pos++;
        while (atom_str[pos] != '"') {
            sb_append(&sb, atom_str[pos]);
            pos++;
        }
        pos++;
        atom->atom.type = TYPE_STRING;
        atom->atom.value.string = sb.items;
    } else { // TYPE_FUNCTION, TYPE_BOOLEAN
        while (!isblank(atom_str[pos]) && atom_str[pos] != '(' && atom_str[pos] != ')') {
            sb_append(&sb, atom_str[pos]);
            pos++;
        }
        sb_append_null(&sb);

        if (strcmp(sb.items, "true") == 0) {
            atom->atom.type = TYPE_BOOLEAN;
            atom->atom.value.boolean = true;
            sb_free(sb);
        } else if (strcmp(sb.items, "false") == 0) {
            atom->atom.type = TYPE_BOOLEAN;
            atom->atom.value.boolean = false;
            sb_free(sb);
        } else {
            atom->atom.type = TYPE_FUNCTION;
            atom->atom.value.function = sb.items;
        }
    }

    *_pos = pos;
    return atom;
}

LispObject *parse_list(char *list_str, size_t *_pos);

LispObject *parse_object(char *obj_str, size_t *_pos)
{
    size_t pos = *_pos;
    LispObject *object = NULL;

    if (obj_str[pos] == '(') {
        object = parse_list(obj_str, &pos);
    } else {
        object = parse_atom(obj_str, &pos);
    }

    *_pos = pos;
    return object;
}

LispObject *parse_list(char *list_str, size_t *_pos)
{
    size_t pos = *_pos;
    LispObject *list = create_list();

    pos++;
    while (list_str[pos] != ')') {
        while (isspace(list_str[pos])) {
            pos++;
            continue;
        }

        if (list_str[pos] == ')') break;

        LispObject *object = parse_object(list_str, &pos);
        add_to_list(list, object);
    }
    pos++;

    *_pos = pos;
    return list;
}

LispObjects parse_program(char *program_str)
{
    LispObjects program = {0};
    size_t pos = 0;

    while (program_str[pos] != '\0') {
        while (isspace(program_str[pos])) {
            pos++;
            continue;
        }

        if (program_str[pos] == '\0') break;

        if (program_str[pos] != '(') {
            printf("ERROR: Top level expression is not a list, it starts with '%c'\n", program_str[pos]);
            exit(1);
        }

        LispObject *list = parse_list(program_str, &pos);
        da_push(&program, list);
    }

    return program;
}

void print_object(const LispObject *o)
{
    assert(o && "Printing NULL object");
    if (o->is_list) {
        printf("(");
        for (size_t i = 0; i < o->list.count; i++) {
            print_object(o->list.items[i]);
            if (i < o->list.count-1) printf(" ");
        }
        printf(")");
    } else {
        switch (o->atom.type) {
        case TYPE_FUNCTION: printf("%s", o->atom.value.function); break;
        case TYPE_INTEGER: printf("%d", o->atom.value.integer); break;
        case TYPE_BOOLEAN: printf("%s", o->atom.value.boolean ? "true" : "false"); break;
        case TYPE_STRING: printf("\"%s\"", o->atom.value.string); break;

        case __types_count:
        default:
            printf("Unreachable atom type %d\n", o->atom.type);
            exit(1);
        }
    }
}

typedef LispObject *(*LispFn)(LispObject *args);

typedef struct
{
    const char *name;
    LispFn fn;
} LispFunction;

#define LispBuiltinFunction(function_name) \
    LispObject *builtin_ ##function_name(LispObject *args) \
    { \
        assert(args && "NULL object passed to builtin function "#function_name); \
        assert(args->is_list && "Object passed to builtin function "#function_name" is not a list"); \
        const char *fn_name = args->list.items[0]->atom.value.function; (void)fn_name; \

LispBuiltinFunction(print)
    for (size_t i = 1; i < args->list.count; i++) {
        print_object(args->list.items[i]);
        printf("\n");
    }
    return create_nil();
}

LispBuiltinFunction(math)
    if (args->list.count-1 < 2) {
        printf("ERROR: number of arguments mismatch, wanted at least 2 but got %zu\n", args->list.count-1);
        exit(1);
    }
    int accumulator = 0;
    for (size_t i = 1; i < args->list.count; i++) {
        LispObject *arg = args->list.items[i];
        if (arg->is_list || arg->atom.type != TYPE_INTEGER) {
            printf("ERROR: arguments mismatch, wanted integer but got object: ");
            print_object(arg);
            printf("\n");
            exit(1);
        }
        if (accumulator == 0) accumulator = arg->atom.value.integer;
        else {
            if (strcmp(fn_name, "+") == 0) {
                accumulator += arg->atom.value.integer;
            } else if (strcmp(fn_name, "-") == 0) {
                accumulator -= arg->atom.value.integer;
            } else if (strcmp(fn_name, "*") == 0) {
                accumulator *= arg->atom.value.integer;
            } else if (strcmp(fn_name, "/") == 0) {
                accumulator /= arg->atom.value.integer;
            } else {
                printf("TODO: math function `%s`\n", fn_name);
                exit(1);
            }
        }
    }
    LispObject *result = create_atom(TYPE_INTEGER);
    result->atom.value.integer = accumulator;
    return result;
}

LispFunction builtin_functions[] = {
    {.name="print", .fn=builtin_print},
    {.name="+",     .fn=builtin_math},
    {.name="-",     .fn=builtin_math},
    {.name="*",     .fn=builtin_math},
    {.name="/",     .fn=builtin_math},
};
const size_t builtin_functions_count = sizeof(builtin_functions)/sizeof(builtin_functions[0]);

LispFn get_function_by_name(const char *name)
{
    for (size_t i = 0; i < builtin_functions_count; i++) {
        if (strcmp(builtin_functions[i].name, name) == 0) return builtin_functions[i].fn;
    }
    return NULL;
}

LispObject *eval(LispObject *o)
{
    assert(o);

#ifdef DEBUG
    printf("Evaluating object ");
    print_object(o);
    printf("\n");
#endif

    LispObject *result = NULL;

    if (o->is_list) {
        if (o->list.count == 0) {
            result = o;
        } else {
            LispObject *ofn = o->list.items[0];
            if (ofn->is_list || ofn->atom.type != TYPE_FUNCTION) {
                printf("\nERROR: Cannot evaluate non function object\n");
                printf("NOTE: Offending object: ");
                print_object(ofn);
                printf("\n");
                exit(1);
            }
            LispFn fn = get_function_by_name(ofn->atom.value.function);
            if (!fn) {
                printf("\nERROR: Unknown function `%s`\n", ofn->atom.value.function);
                exit(1);
            }

            for (size_t i = 1; i < o->list.count; i++) {
                if (!o->list.items[i]->is_list) continue;
                LispObject *evaluated = eval(o->list.items[i]);
                if (evaluated != o->list.items[i]) {
                    free(o->list.items[i]);
                    o->list.items[i] = evaluated;
                }
            }

            printf("Executing function `%s`\n", ofn->atom.value.function);
            result = fn(o);
        }
    } else {
        result = o;
    }
     
    return result;
}

void test(void)
{
    LispObject *L = create_list();

    //LispObject *af = create_atom(TYPE_FUNCTION);
    //af->atom.value.function = strdup("palle");
    //add_to_list(L, af);

    LispObject *ai = create_atom(TYPE_INTEGER);
    ai->atom.value.integer = 69;
    add_to_list(L, ai);

    LispObject *ab = create_atom(TYPE_BOOLEAN);
    ab->atom.value.boolean = true;
    add_to_list(L, ab);

    LispObject *as = create_atom(TYPE_STRING);
    as->atom.value.string = strdup("Ciao caro");
    add_to_list(L, as);

    printf("List = ");
    print_object(L);
    printf("\n");

    printf("Evaluation: =======================\n");
    LispObject *result = eval(L);
    printf("===================================\n");

    printf("Evaluated List = ");
    print_object(result);
    printf("\n");
}

void addToHistory(char *input) { printf("- Adding input to history: %s\n", input); }

void usage()
{
    printf("Usage: ./lisp [file]\n");
}

int main(int argc, char **argv)
{
    if (argc == 1) { // REPL MODE
        printf("\nLisp Version 0.0.1\n\n");
        char buffer[1024];
        char *input = NULL;
        while (true) {
            printf("> ");
            fflush(stdout);

            memset(buffer, 0, sizeof(buffer));            
            input = fgets(buffer, sizeof(buffer), stdin);

            if (!input) continue;
            input[strlen(input)-1] = '\0';
            if (strlen(input) == 0) continue;

            if (strcmp(input, "quit") == 0 || strcmp(input, "q") == 0) break;

            printf("You wrote: \"%s\"\n\n", input);
        }
    } else if (argc == 2) { // INTERPRETER MODE
        const char *filepath = argv[1];
        Nob_String_Builder sb = {0};
        if (!nob_read_entire_file(filepath, &sb)) {
            printf("\nERROR: Could not read file `%s`\n", filepath);
            return 1;
        }
        sb_append_null(&sb);
        char *program_str = sb.items;

        LispObjects program = parse_program(program_str);

        printf("Program has been parsed into %zu expression%s\n", program.count, program.count == 1 ? "" : "s");

        printf("Evaluating program:\n\n");
        for (size_t i = 0; i < program.count; i++)
            eval(program.items[i]);

    } else {
        usage();
        return 1;
    }

    return 0;
}
