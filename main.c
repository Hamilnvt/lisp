// TODO:
// - create some examples
// - add directives: commands that start with !
//   > !debug enables debug prints
// - quote '
// - list

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dynamic_arrays.h"

#define NOB_IMPLEMENTATION
#include "nob.h"

static bool debug_enabled = true;
bool in_repl = false;

#define DEBUG (debug_enabled && !in_repl)
#define DEBUG_PRINT(fmt, ...) \
    do { if (DEBUG) { printf("[DEBUG] " fmt "\n", ##__VA_ARGS__); } } while (0)

typedef enum {
    TYPE_NIL,
    TYPE_SYMBOL,
    TYPE_INTEGER,
    TYPE_BOOLEAN,
    TYPE_STRING,
    __types_count
} Type;

const char *type_to_string(Type type)
{
    switch (type) {
    case TYPE_NIL:     return "Nil";
    case TYPE_SYMBOL:  return "Symbol";
    case TYPE_INTEGER: return "Integer";
    case TYPE_BOOLEAN: return "Boolean";
    case TYPE_STRING:  return "String";

    case __types_count:
    default:
        printf("Unreachable type %d in type_to_string\n", type);
        abort();
    }
}

typedef enum
{
    SYM_UNKNOWN = -1,
    SYM_NULL = 0,
    SYM_SPECIAL_FORM,
    SYM_BUILTIN_FUNCTION,
    SYM_USER_FUNCTION,
    SYM_VARIABLE,
    __symbol_kinds_cout
} SymbolKind;

typedef struct BuiltinFunction BuiltinFunction;
typedef struct BuiltinFunction SpecialForm;
typedef struct UserFunction UserFunction;
typedef struct Variable Variable;

typedef struct
{
    const char *name;
    SymbolKind kind;
    union {
        SpecialForm *sf;
        BuiltinFunction *bf;
        UserFunction *uf;
        Variable *var;
    };
} Symbol;

typedef union {
    int integer;
    bool boolean;
    char *string;
    Symbol symbol;
} Value;

typedef struct {
    Type type;
    Value value;
} Atom;

typedef struct Expression {
    bool is_list;
    bool is_quoted;
    union {
        Atom atom;
        struct {
            struct Expression *this;
            struct Expression *next;
        } list;
    };
} Expression;

typedef struct {
    Expression *expr;
    bool error;
    char message[4096];
} Result;

static inline Result make_result(Expression *e) { return (Result){.expr=e}; }

static inline Result make_error(char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    Result err = {.error=true};
    vsnprintf(err.message, sizeof(err.message), fmt, args);

    va_end(args);
    return err;
}

static inline Value *value(Expression *e)
{
    assert(e);
    assert(!e->is_list);
    return &e->atom.value;
}

static inline Type type(Expression *e)
{
    assert(e);
    assert(!e->is_list);
    return e->atom.type;
}

static inline Expression *this(Expression *e)
{
    assert(e);
    assert(e->is_list);
    return e->list.this;
}

static inline Expression *next(Expression *e)
{
    assert(e);
    assert(e->is_list);
    return e->list.next;
}

static inline size_t list_len(Expression *list)
{
    if (!list) return 0;
    assert(list->is_list);
    size_t len = 0;
    while (list && this(list)) {
        len++;
        list = next(list);
    }
    return len;
}

static inline bool list_is_empty(Expression *list) { return list_len(list) == 0; }

static inline Expression *list_at(Expression *list, int at)
{
    size_t len = list_len(list);
    assert(at >= -(int)len && at < (int)len && "List access out of bounds");

    Expression *result = NULL;
    Expression *it = NULL;
    if (at >= 0) {
        size_t i = 0;
        it = list;
        while (i < (size_t)at) {
            it = next(it);
            i++;
        }
        result = this(it);
    } else {
        printf("TODO: list negative index not yet implemented\n");
        exit(1);
    }
    return result;
}

#define for_list(list) \
    for (Expression *it = (list); it && this(it); it = next(it))

Expression *create_expression(void)
{
    Expression *e = calloc(1, sizeof(Expression));
    return e;
}

Expression *create_atom(Type type)
{
    Expression *e = create_expression();
    e->atom.type = type;
    return e;
}

static inline Expression *create_nil() { return create_atom(TYPE_NIL); }

Expression *create_list()
{
    Expression *e = calloc(1, sizeof(Expression));
    e->is_list = true;
    return e;
}

static inline void set_this(Expression *e, Expression *item) { e->list.this = item; }

void add_to_list(Expression *list, Expression *item)
{
    assert(list);
    assert(list->is_list);
    assert(item);

    if (this(list)) { // Not the first element
        while (next(list)) list = next(list);
        Expression *new = create_list();
        set_this(new, item);
        list->list.next = new;
    } else { // First element of list
        set_this(list, item);
    }
}

Result parse_atom(char *atom_str, size_t *_pos)
{
    size_t pos = *_pos;
    Expression *atom = create_expression();
    Nob_String_Builder sb = {0};

    if (isdigit(atom_str[pos])) { // TYPE_INTEGER
        while (isdigit(atom_str[pos])) {
            sb_append(&sb, atom_str[pos]);
            pos++;
        }
        if (!isspace(atom_str[pos]) && atom_str[pos] != '(' && atom_str[pos] != ')') {
            return make_error("Number has trailing characters starting with '%c'", atom_str[pos]);
        }
        sb_append_null(&sb);
        atom->atom.type = TYPE_INTEGER;
        value(atom)->integer = atoi(sb.items);
    } else if (atom_str[pos] == '"') { // TYPE_STRING
        pos++;
        while (atom_str[pos] != '"') {
            sb_append(&sb, atom_str[pos]);
            pos++;
        }
        pos++;
        atom->atom.type = TYPE_STRING;
        value(atom)->string = sb.items;
    } else { // TYPE_SYMBOL, TYPE_BOOLEAN
        while (!isspace(atom_str[pos]) && atom_str[pos] != '(' && atom_str[pos] != ')') {
            sb_append(&sb, atom_str[pos]);
            pos++;
        }
        sb_append_null(&sb);

        if (strcmp(sb.items, "nil") == 0) {
            atom->atom.type = TYPE_NIL;
            sb_free(sb);
        } else if (strcmp(sb.items, "true") == 0) {
            atom->atom.type = TYPE_BOOLEAN;
            value(atom)->boolean = true;
            sb_free(sb);
        } else if (strcmp(sb.items, "false") == 0) {
            atom->atom.type = TYPE_BOOLEAN;
            value(atom)->boolean = false;
            sb_free(sb);
        } else {
            atom->atom.type = TYPE_SYMBOL;
            Symbol sym = {
                .kind = SYM_NULL,
                .name = sb.items
            };
            value(atom)->symbol = sym;
        }
    }

    *_pos = pos;
    return make_result(atom);
}

Result parse_list(char *list_str, size_t *_pos);

Result parse_expression(char *expr_str, size_t *_pos)
{
    size_t pos = *_pos;
    Result result = {0};

    if (expr_str[pos] == '(') {
        result = parse_list(expr_str, &pos);
    } else {
        result = parse_atom(expr_str, &pos);
    }

    *_pos = pos;
    return result;
}

Result parse_list(char *list_str, size_t *_pos)
{
    size_t pos = *_pos;
    if (list_str[pos] != '(') {
        return make_error("Expecting '(' but got '%c'", list_str[pos]);
    }
    pos++;

    Expression *list = create_list();

    while (list_str[pos] != ')') {
        while (isspace(list_str[pos])) {
            pos++;
            continue;
        }

        if (list_str[pos] == ')') break;

        Result r = parse_expression(list_str, &pos);
        if (r.error) return r;
        add_to_list(list, r.expr);
    }
    pos++;

    *_pos = pos;
    return make_result(list);
}

void print_symbol(Symbol sym);
void print_expression(Expression *e)
{
    assert(e && "Printing NULL expression");
    if (e->is_list) {
        printf("(");
        for_list(e) {
            print_expression(this(it));
            if (next(it)) printf(" ");
        }
        printf(")");
    } else {
        switch (type(e)) {
        case TYPE_NIL:     printf("nil");                                      break;
        case TYPE_SYMBOL:  printf(value(e)->symbol.name);                      break;
        case TYPE_INTEGER: printf("%d", value(e)->integer);                    break;
        case TYPE_BOOLEAN: printf("%s", value(e)->boolean ? "true" : "false"); break;
        case TYPE_STRING:  printf("\"%s\"", value(e)->string);                 break;

        case __types_count:
        default:
            printf("Unreachable atom type %d in print_expression\n", type(e));
            abort();
        }
    }
}

Result parse_program(char *program_str)
{
    Expression *program = create_list();
    size_t pos = 0;

    while (program_str[pos] != '\0') {
        while (isspace(program_str[pos])) {
            pos++;
            continue;
        }

        if (program_str[pos] == '\0') break;

        if (program_str[pos] != '(') {
            return make_error("ERROR: Top level expression is not a list, it starts with '%c'", program_str[pos]);
        }

        Result r = parse_list(program_str, &pos);
        if (r.error) return r;
        add_to_list(program, r.expr);
    }

    return make_result(program);
}

typedef Result (*Fn)(Expression *args);

struct BuiltinFunction {
    const char *name;
    Fn fn;
};

#define DefineSpecialForm(form_name) \
    Result special_form_ ##form_name(Expression *args) \
    { \
        assert(args && "NULL expression passed to special form "#form_name); \
        assert(args->is_list && "Expression passed to special form "#form_name" is not a list"); \
        const char *name = value(this(args))->symbol.name; (void)name; \
        args = next(args); \
        size_t args_count = list_len(args); (void)args_count; \

bool to_boolean(Expression *e)
{
    assert(e);
    if (e->is_list) {
        return list_len(e) > 0;
    } else {
        switch (type(e)) {
        case TYPE_NIL:      return false;
        case TYPE_SYMBOL:   return true;
        case TYPE_INTEGER:  return value(e)->integer > 0;
        case TYPE_BOOLEAN:  return value(e)->boolean;
        case TYPE_STRING:   return strlen(value(e)->string) > 0;

        case __types_count:
        default:
            printf("Unreachable atom type %d in to_boolean\n", type(e));
            abort();
        }
    }
}

Result eval(Expression *e);

DefineSpecialForm(if)
    if (args_count != 3) {
        return make_error("Number of arguments mismatch, wanted 3 but got %zu", args_count);
    }
    Expression *condition = list_at(args, 0);
    Result eval_result = eval(condition);
    if (eval_result.error) return eval_result;
    Expression *evaluated_condition = eval_result.expr;

    if (DEBUG) {
        printf("[DEBUG] Condition `");
        print_expression(condition);
        printf("` has been evaluated to ");
        print_expression(evaluated_condition);
        printf("\n");
    }

    Expression *tt = list_at(args, 1);
    Expression *ff = list_at(args, 2);
    return eval(to_boolean(evaluated_condition) ? tt : ff);
}

typedef struct
{
    char **items;
    size_t count;
    size_t capacity;
} CStrings;

struct UserFunction {
    const char *name;
    CStrings params;
    Expression *body;
};

struct
{
    UserFunction *items;
    size_t count;
    size_t capacity;
} user_functions = {0};

UserFunction *get_user_function(const char *name)
{
    for (size_t i = 0; i < user_functions.count; i++) {
        if (strcmp(user_functions.items[i].name, name) == 0) return &user_functions.items[i];
    }
    return NULL;
}

SpecialForm *get_special_form(const char *name);
BuiltinFunction *get_builtin_function(const char *name);
Variable *get_variable(const char *name);
Symbol *get_symbol(const char *name)
{
    Symbol *found = calloc(1, sizeof(Symbol));
    assert(found);

    SpecialForm *sf = get_special_form(name);
    if (sf) {
        found->kind = SYM_SPECIAL_FORM;
        found->sf = sf;
        return found;
    }

    BuiltinFunction *bf = get_builtin_function(name);
    if (bf) {
        found->kind = SYM_BUILTIN_FUNCTION;
        found->bf = bf;
        return found;
    }

    UserFunction *uf = get_user_function(name);
    if (uf) {
        found->kind = SYM_USER_FUNCTION;
        found->uf = uf;
        return found;
    }

    Variable *var = get_variable(name);
    if (var) {
        found->kind = SYM_VARIABLE;
        found->var = var;
        return found;
    }

    return NULL;
}

DefineSpecialForm(defun)
    if (args_count != 3) {
        return make_error("Number of arguments mismatch, wanted 3 but got %zu", args_count);
    }

    Expression *def_name = list_at(args, 0); // TODO: it can be evaluated, could be powerful
    if (def_name->is_list || type(def_name) != TYPE_SYMBOL) {
        return make_error("Expecting name of function to be a symbol, but got %s",
                def_name->is_list ? "List" : type_to_string(type(def_name)));
    }
    Symbol *redefined = get_symbol(value(def_name)->symbol.name);
    if (redefined) {
        // TODO: print kind as string
        return make_error("Redefinition of symbol `%s`, first defined as a %d", value(def_name)->symbol.name,
                redefined->kind);
    }

    Expression *def_params_list = list_at(args, 1);
    if (!def_params_list->is_list) {
        return make_error("Expecting parameters list to be a list, but got %s", type_to_string(type(def_params_list)));
    }
    CStrings params = {0};
    for_list(def_params_list) {
        Expression *param = this(it);
        if (param->is_list || type(param) != TYPE_SYMBOL) {
            return make_error("Expecting parameter %zu to be a symbol, but got %s", params.count+1,
                    param->is_list ? "List" : type_to_string(type(param)));
        }
        da_push(&params, strdup(value(param)->symbol.name));
    }

    Expression *def_body = list_at(args, 2);
    if (!def_body->is_list) {
        return make_error("Expecting body of function to be a list, but got %s", type_to_string(type(def_body)));
    }

    if (DEBUG) {
        printf("[DEBUG] Defining function `");
        print_expression(def_name);
        printf("`:\n");
        printf("[DEBUG] Parameters: ");
        print_expression(def_params_list);
        printf("\n");
        printf("[DEBUG] Body: ");
        print_expression(def_body);
        printf("\n");
    }

    UserFunction uf = {
        .name = strdup(value(def_name)->symbol.name),
        .params = params,
        .body = def_body
    };

    da_push(&user_functions, uf);

    return make_result(create_nil());
}

struct Variable {
    const char *name;
    Expression *value;
};

Variable *create_var(const char *name, Expression *value)
{
    Variable *var = calloc(1, sizeof(Variable));
    var->name = strdup(name);
    var->value = value;
    assert(var);
    return var;
}

typedef struct
{
    Variable **items; 
    size_t count;
    size_t capacity;
} Variables;
static Variables variables = {0};

DefineSpecialForm(let)
    if (args_count != 2) {
        return make_error("Number of arguments mismatch, wanted 2 but got %zu", args_count);
    }

    Expression *var_name = list_at(args, 0); // TODO: it can be evaluated, could be powerful
    if (var_name->is_list || type(var_name) != TYPE_SYMBOL) {
        return make_error("Expecting name of variable to be a symbol, but got %s",
                var_name->is_list ? "List" : type_to_string(type(var_name)));
    }
    Symbol *redefined = get_symbol(value(var_name)->symbol.name);
    if (redefined) {
        // TODO: print kind as string
        return make_error("Redefinition of symbol `%s`, first defined as a %d", value(var_name)->symbol.name,
                redefined->kind);
    }

    Expression *var_value = list_at(args, 1);
    Result value_result = eval(var_value);
    if (value_result.error) return value_result;
    var_value = value_result.expr;

    Variable *var = create_var(value(var_name)->symbol.name, var_value);
    da_push(&variables, var);

    if (DEBUG) {
        printf("[DEBUG] Defining variable `%s`: ", var->name);
        print_expression(var->value);
        printf("\n");
    }

    return make_result(create_nil());
}

Variable *get_variable(const char *name)
{
    for (size_t i = 0; i < variables.count; i++) {
        if (strcmp(name, variables.items[i]->name) == 0) return variables.items[i];
    }
    return NULL;
}

SpecialForm special_forms[] = {
    {.name="if",    .fn=special_form_if},
    {.name="let",   .fn=special_form_let},
    {.name="defun", .fn=special_form_defun},
};
const size_t special_forms_count = sizeof(special_forms)/sizeof(special_forms[0]);

SpecialForm *get_special_form(const char *name)
{
    for (size_t i = 0; i < special_forms_count; i++) {
        if (strcmp(special_forms[i].name, name) == 0) return &special_forms[i];
    }
    return NULL;
}

#define DefineBuiltinFunction(function_name) \
    Result builtin_ ##function_name(Expression *args) \
    { \
        assert(args && "NULL expression passed to builtin function "#function_name); \
        assert(args->is_list && "Expression passed to builtin function "#function_name" is not a list"); \
        const char *fn_name = value(this(args))->symbol.name; (void)fn_name; \
        args = next(args); \
        size_t args_count = list_len(args); (void)args_count; \

DefineBuiltinFunction(print)
    for_list(args) {
        print_expression(this(it));
        printf("\n");
    }
    return make_result(create_nil());
}

DefineBuiltinFunction(math_plus_and_mult)
    if (args_count < 2) {
        return make_error("Number of arguments mismatch, wanted at least 2 but got %zu", args_count);
    }
    int accumulator = 0;
    for_list(args) {
        Expression *arg = this(it);
        if (arg->is_list || type(arg) != TYPE_INTEGER) {
            printf("ERROR: arguments mismatch, wanted integer but got expression: ");
            print_expression(arg);
            printf("\n");
            printf("\nTODO: return Result rather than exiting the program\n");
            exit(1);
        }
        if (accumulator == 0) accumulator = value(arg)->integer;
        else {
            if (strcmp(fn_name, "+") == 0) {
                accumulator += value(arg)->integer;
            } else if (strcmp(fn_name, "*") == 0) {
                accumulator *= value(arg)->integer;
            } else {
                return make_error("Unknown math function `%s`", fn_name);
            }
        }
    }
    Expression *result = create_atom(TYPE_INTEGER);
    value(result)->integer = accumulator;
    return make_result(result);
}

BuiltinFunction builtin_functions[] = {
    {.name="print", .fn=builtin_print},
    {.name="+",     .fn=builtin_math_plus_and_mult},
    {.name="*",     .fn=builtin_math_plus_and_mult},
};
const size_t builtin_functions_count = sizeof(builtin_functions)/sizeof(builtin_functions[0]);

BuiltinFunction *get_builtin_function(const char *name)
{
    for (size_t i = 0; i < builtin_functions_count; i++) {
        if (strcmp(builtin_functions[i].name, name) == 0) return &builtin_functions[i];
    }
    return NULL;
}

static inline Result evaluate_special_form(SpecialForm *sf, Expression *args)
{
    DEBUG_PRINT("Executing special form `%s`", sf->name);
    return sf->fn(args);
}

Result evaluate_builtin_function(BuiltinFunction *bf, Expression *args)
{
    for_list(args) {
        if (it == args) continue;
        Result eval_arg = eval(this(it));
        if (eval_arg.error) return eval_arg;
        set_this(it, eval_arg.expr);
    }

    DEBUG_PRINT("Executing builtin function `%s`", bf->name);
    return bf->fn(args);
}

Expression *clone_expression(Expression *e)
{
    Expression *c = create_expression();
    if (e->is_list) {
        c->is_list = true;
        for_list(e) {
            add_to_list(c, clone_expression(this(it)));
        } 
    } else {
        Value *v = value(e);
        c->atom.type = type(e);
        switch (type(e)) {
        case TYPE_NIL:                                                                                       break;
        case TYPE_SYMBOL:  value(c)->symbol  = (Symbol){.kind=v->symbol.kind, .name=strdup(v->symbol.name)}; break;
        case TYPE_INTEGER: value(c)->integer = v->integer;                                                   break;
        case TYPE_BOOLEAN: value(c)->boolean = v->boolean;                                                   break;
        case TYPE_STRING:  value(c)->string  = strdup(v->string);                                            break;
        case __types_count:
        default:
            printf("Unreachable type %d in clone_expression\n", type(e));
            abort();
        }
    }
    return c;
}

void free_expression(Expression *e)
{
    if (e->is_list) {
        for_list(e) {
            free_expression(this(it));
        }
    } else {
        Value *v = value(e);
        switch (type(e)) {
        case TYPE_NIL:
        case TYPE_INTEGER:
        case TYPE_BOOLEAN:
            break;
        case TYPE_SYMBOL:  free((char *)v->symbol.name); break;
        case TYPE_STRING:  free(v->string);  break;
        case __types_count:
        default:
            printf("Unreachable type %d in clone\n", type(e));
            abort();
        }
    }
    free(e);
}

void replace_args_in_body(Expression **body, CStrings params, Expression *args)
{
    if ((*body)->is_list) {
        for_list(*body) {
            replace_args_in_body(&it->list.this, params, args);
        }
    } else if (type(*body) == TYPE_SYMBOL) {
        for (size_t i = 0; i < params.count; i++) {
            if (strcmp(value(*body)->symbol.name, params.items[i]) == 0) {
                free_expression(*body);
                *body = list_at(args, i);
                return;
            }
        }
    }
}

Result evaluate_user_function(UserFunction *uf, Expression *args)
{
    args = next(args);
    for_list(args) {
        Result eval_arg = eval(this(it));
        if (eval_arg.error) return eval_arg;
        set_this(it, eval_arg.expr);
    }
    Expression *body = clone_expression(uf->body);
    replace_args_in_body(&body, uf->params, args);
    return eval(body);
}

void print_symbol(Symbol sym)
{
    switch (sym.kind) {
    case SYM_SPECIAL_FORM: {
        printf("Special form `%s`\n", sym.name);
        printf("TODO: description\n");
    } break; 
    case SYM_BUILTIN_FUNCTION: {
        printf("Builtin function `%s`\n", sym.name);
        printf("TODO: description\n");
    } break;
    case SYM_USER_FUNCTION: {
        printf("Builtin function `%s`\n", sym.name);
        printf("Arguments: (");
        for (size_t i = 0; i < sym.uf->params.count; i++) {
            printf("%s", sym.uf->params.items[i]);
            if (i != sym.uf->params.count-1) printf(" ");
        }
        printf(")\n");
        printf("Body: ");
        print_expression(sym.uf->body);
        printf("\n");
    } break;
    case SYM_VARIABLE: {
        printf("Variable `%s`: ", sym.name);
        print_expression(sym.var->value);
        printf("\n");
    } break;

    case SYM_UNKNOWN:
    case __symbol_kinds_cout:
    default:
        printf("Unreachable symbol kind '%d' in print_symbol\n", sym.kind);
        abort();
    }
}

void resolve_symbol(Symbol *sym)
{
    if (sym->kind != SYM_NULL) return;

    SpecialForm *sf = get_special_form(sym->name);
    if (sf) {
        sym->kind = SYM_SPECIAL_FORM;
        sym->sf = sf;
        return;
    }

    BuiltinFunction *bf = get_builtin_function(sym->name);
    if (bf) {
        sym->kind = SYM_BUILTIN_FUNCTION;
        sym->bf = bf;
        return;
    }

    UserFunction *uf = get_user_function(sym->name);
    if (uf) {
        sym->kind = SYM_USER_FUNCTION;
        sym->uf = uf;
        return;
    }

    Variable *var = get_variable(sym->name);
    if (var) {
        sym->kind = SYM_VARIABLE;
        sym->var = var;
        return;
    }

    sym->kind = SYM_UNKNOWN;
}

Result eval(Expression *e)
{
    assert(e);

    if (e->is_quoted) return make_result(e);

    if (DEBUG) {
        printf("[DEBUG] Evaluating expression ");
        print_expression(e);
        printf("\n");
    }

    if (e->is_list) {
        if (list_is_empty(e)) {
            return make_result(e);
        } else {
            Expression *e_sym = this(e);
            if (e_sym->is_list || type(e_sym) != TYPE_SYMBOL) {
                printf("\nERROR: Cannot evaluate non symbol expression\n");
                printf("NOTE: Offending expression: ");
                print_expression(e_sym);
                printf("\n");
                printf("\nTODO: return Result rather than exiting the program\n");
                exit(1);
            }
            Symbol *sym = &value(e_sym)->symbol;
            resolve_symbol(sym);
            if (sym->kind == SYM_UNKNOWN) {
                return make_error("Unknown symbol `%s`", sym->name);
            }
            switch (sym->kind) {
            case SYM_SPECIAL_FORM:     return evaluate_special_form(sym->sf, e);
            case SYM_BUILTIN_FUNCTION: return evaluate_builtin_function(sym->bf, e);
            case SYM_USER_FUNCTION:    return evaluate_user_function(sym->uf, e);
            case SYM_VARIABLE:         return make_result(sym->var->value); // TODO: I'm not sure, here I should evaluate the variable and checking what it contains (function)

            case SYM_NULL:
            case SYM_UNKNOWN:
            case __symbol_kinds_cout:
            default:
                printf("Unreachable symbol kind '%d' in eval\n", sym->kind);
                abort();
            }
        }
    } else {
        if (type(e) == TYPE_SYMBOL) {
            Symbol *sym = &value(e)->symbol;
            resolve_symbol(sym);
            if (sym->kind == SYM_VARIABLE) {
                return make_result(sym->var->value);
            } else {
                return make_result(e);
            }
        } else {
            return make_result(e);
        }
    }

    return make_error("Unreachable control flow in eval");
}

void usage()
{
    printf("Usage: ./lisp [file]\n");
}

int main(int argc, char **argv)
{
    if (argc == 1) { // REPL MODE
        in_repl = true;

        printf("\nLisp (version 0.0.1)\n");
        char buffer[1024];
        char *input = NULL;
        while (true) {
            printf("\n");
            printf("> ");
            fflush(stdout);

            memset(buffer, 0, sizeof(buffer));
            input = fgets(buffer, sizeof(buffer), stdin);

            if (!input) continue;
            input[strlen(input)-1] = '\0';
            if (strlen(input) == 0) continue;

            if (input[0] == '!') {
                input++;
                if (strcmp(input, "quit") == 0 || strcmp(input, "q") == 0) break;
                else if (strcmp(input, "load") == 0 || strcmp(input, "l") == 0) {
                    printf("TODO: load command is not yet implemented\n");
                } else {
                    printf("ERROR: Unknown command `%s`\n", input);
                }
            } else {
                size_t pos = 0;
                Result parse_result = parse_expression(input, &pos);
                if (parse_result.error) {
                    printf("Parse error: %s\n", parse_result.message);
                    continue;
                }
                Expression *parsed_expr = parse_result.expr;

                if (!parsed_expr->is_list && type(parsed_expr) == TYPE_SYMBOL) {
                    Symbol *sym = &value(parsed_expr)->symbol;
                    resolve_symbol(sym);
                    if (sym->kind == SYM_UNKNOWN) {
                        printf("Error: Unknown symbol `%s`\n", sym->name);
                        continue;
                    }
                    print_symbol(value(parsed_expr)->symbol);
                    continue;
                }

                Result eval_result = eval(parsed_expr);
                if (eval_result.error) {
                    printf("Evaluation error: %s\n", eval_result.message);
                    continue;
                }

                print_expression(eval_result.expr);

                printf("\n");
            }
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

        Result parse_result = parse_program(program_str);
        if (parse_result.error) {
            printf("Parse error: %s\n", parse_result.message);
            return 1;
        }
        Expression *program = parse_result.expr;
        size_t expressions_count = list_len(program);

        DEBUG_PRINT("Program has been parsed into %zu expression%s\n", expressions_count,
                expressions_count == 1 ? "" : "s");

        printf("Evaluating program:\n\n");
        for_list(program) {
            Result eval_result = eval(this(it));
            if (eval_result.error) {
                printf("Evaluation error: %s\n", eval_result.message);
                return 1;
            }
        }

    } else {
        usage();
        return 1;
    }

    return 0;
}
