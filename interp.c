#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>

#include "words.h"

#define MAX_BLOCK_STACK_SIZE 16
#define MAX_VARS 128
#define MAX_STACK_SIZE 2048
#define MAX_SUBROUTINES 256
#define MAX_CALL_STACK_SIZE 64 // maybe increase for recursion

enum block_type {
    BLOCK_IF = 1,
    BLOCK_WHILE,
};

typedef struct block_item {
    enum block_type type;
    union {
    };
} block_item;

enum value_type {
    VAL_NULL,
    VAL_NUM,
    VAL_STR,
};
typedef struct value_t {
    enum value_type type;
    union {
        float num;
        char* str;
    };
} value_t;

bool is_value_true(value_t v) {
    switch (v.type) {
    case VAL_NULL:
        return false;
    case VAL_NUM:
        return (bool)v.num;
    case VAL_STR:
        return v.str != NULL && strlen(v.str) != 0;
    default:
        return false;
    }
}

#define NEW_BLOCK_IF (block_item){ BLOCK_IF }

struct subroutine_info {
    int start, end;
};

struct state_t {
    bool skip; // whether to skip execution (ex. to skip code in an `if` statement after a false value)
    int skip_depth; // used to keep track of when we should stop skipping.
                    // ex. when skipping an `if` statement, finding another
                    // `if` inside will increment this counter, and finding an
                    // `endif` will decrement. If we find an `endif` and the
                    // counter is 0 then we stop skipping

    // block stack
    block_item block_stack[MAX_BLOCK_STACK_SIZE];
    int block_stack_ptr;

    // variable map
    int var_count;
    char* var_names[MAX_VARS];
    value_t var_values[MAX_VARS];

    // value stack
    value_t stack[MAX_STACK_SIZE];
    int stack_ptr;

    // subroutines
    int subroutine_count;
    char* subroutine_names[MAX_SUBROUTINES];
    struct subroutine_info subroutine_infos[MAX_SUBROUTINES];

    struct {
        char* name;
        int start;
    } current_subroutine;

    // call stack
    int call_stack[MAX_CALL_STACK_SIZE];
    int call_stack_ptr;

    // code
    char **lines;
    int linecount;
    int lineptr;
};

struct state_t state = {0};
bool debug_pushes = false;
bool debug_pops = false;
bool debug_lines = false;

// generic stack functions
#define GEN_STACK_PUSH(val, stack, stackptr, size) \
    { \
        if (stackptr >= size) { \
            printf("gah! stack overflow in " #stack "!\n"); exit(1); \
        } \
        stack[stackptr++] = val; \
    }

#define GEN_STACK_POP(stack, stackptr, type) \
    ((stackptr > 0) ? stack[--stackptr] : (printf("gah! stack underflow in `" #stack "`!\n"), exit(1), (type){0}))


void push_block(block_item block) {
    GEN_STACK_PUSH(block, state.block_stack, state.block_stack_ptr, MAX_BLOCK_STACK_SIZE);
}
block_item* get_block() {
    return &state.block_stack[state.block_stack_ptr-1];
}
block_item pop_block() {
    return GEN_STACK_POP(state.block_stack, state.block_stack_ptr, block_item);
}

void push_val(value_t val) {
    if (debug_pushes) {
        printf("pushing value %f/'%s'\n",
            val.type==VAL_NUM?val.num:0,
            val.type==VAL_STR?val.str:"");
    }
    GEN_STACK_PUSH(val, state.stack, state.stack_ptr, MAX_STACK_SIZE);
}
value_t pop_val() {
    if (debug_pops)
        printf("pop\n");
    return GEN_STACK_POP(state.stack, state.stack_ptr, value_t);
}

void push_call(int ptr) {
    GEN_STACK_PUSH(ptr, state.call_stack, state.call_stack_ptr, MAX_CALL_STACK_SIZE);
}
int pop_call() {
    return GEN_STACK_POP(state.call_stack, state.call_stack_ptr, int);
}

// get subroutine info from name, returns zero'ed struct if not found (check if name!=NULL)
struct subroutine_info get_subroutine_info(char* name) {
    for (int i=0;i<state.subroutine_count;i++) {
        if (!strcmp(name, state.subroutine_names[i])) {
            return state.subroutine_infos[i];
        }
    }
    return (struct subroutine_info){0};
}

void add_subroutine(char* name, struct subroutine_info info) {
    state.subroutine_names[state.subroutine_count] = name;
    state.subroutine_infos[state.subroutine_count] = info;
    state.subroutine_count++;
    /* printf("\tnew subroutine!\n"); */
    /* printf("\t\tname=%s\n",name); */
    /* printf("\t\tstart=%d\n",info.start); */
    /* printf("\t\tend=%d\n",info.end); */
}

const char* INST_KWS[] = {
    W_PUSHVAR,
    W_POPVAR,
    W_PUSHNUM,
    W_PUSHSTR,
    W_READNUM,
    W_ADD,
    W_SUB,
    W_MUL,
    W_DIV,
    W_MOD,
    W_EQ,
    W_NEQ,
    W_GT,
    W_LT,
    W_GTE,
    W_LTE,
    W_GOSUB,
    W_RETURN,
    W_PRINT,
};

int get_instruction(char *str) {
    for (int i=0;i<sizeof(INST_KWS)/sizeof(char*);i++) {
        /* printf("\ttesting %s vs %s\n",str,INST_KWS[i]); */
        if (!strcmp(str,INST_KWS[i]))
            return i;
    }
    return -1;
}

char* parse_string(char *str) {
    bool escape_next = false;
    char *dest = malloc(strlen(str)+1); // the biggest it can be is the same as the original string
    
    char *ptr = str;
    char *destptr = dest;
    for (; *ptr != 0; ptr++) {
        if (ptr == str) { // if this is the first char...
            // ignore first quote, error if there isnt one
            if (*ptr == '"')
                continue;
            else
                return NULL;
        }

        if (escape_next) {
            if (*ptr == 'n')
                *destptr = '\n';
            else if (*ptr == 't')
                *destptr = '\t';
            else
                *destptr = *ptr;
            destptr++;
            escape_next = false;
            continue;
        }

        if (*ptr == '\\') {
            escape_next = true;
            continue;
        }

        if (*ptr == '"') {
            break;
        }

        *destptr = *ptr;
        destptr++;

    }

    // dest might be too big, so lets make res which is the right size
    char *res = strdup(dest);
    free(dest);

    return res;
}

void do_instruction(char *tok, char *rest) {
    /* printf("\tim a do instructin: '%s' '%s'\n",tok,rest); */
    
    if (!strcmp(tok,W_PUSHNUM)) {
        value_t v = (value_t){.type=VAL_NUM, .num=atof(rest)}; //TODO: error detection AND replace `rest` w/ getting another token
        push_val(v);
    } else
    if (!strcmp(tok,W_PUSHSTR)) {
        char *str = parse_string(rest);
        if (str == NULL) {
            printf("gah! string '%s' was unparsable!\n",rest);
            exit(1);
        }
        value_t v = (value_t){.type=VAL_STR, .str=str};

        push_val(v);
    } else
    if (!strcmp(tok,W_PRINT)) {
        value_t v = pop_val();
        switch (v.type) {
        case VAL_NUM:
            printf("%f\n",v.num); break;
        case VAL_STR:
            printf("%s\n",v.str); break;
        default:
            printf("(null value)\n"); break;
        }
    } else
    if (!strcmp(tok,W_READNUM)) {
        value_t value;
        value.type = VAL_NUM;
        printf("\t\t\t\t\t\tinput nuber: ");
        if (scanf("%f",&value.num) != 1) {
            printf("\t\t\t\t\t\t invalid so 0 >:(");
            value = (value_t){.type=VAL_NUM,.num=0};
        }
        push_val(value);
    } else
    if (!strcmp(tok,W_ADD)) {
        value_t b = pop_val();
        value_t a = pop_val();
        //TODO: assert a.type == VAL_NUM && b.type == VAL_NUM
        push_val((value_t){.type=VAL_NUM, .num = a.num+b.num});
    } else
    if (!strcmp(tok,W_EQ)) {
        value_t b = pop_val();
        value_t a = pop_val();
        //TODO: assert a.type == VAL_NUM && b.type == VAL_NUM
        push_val((value_t){.type=VAL_NUM, .num = a.num == b.num});
    } else
    if (!strcmp(tok,W_GOSUB)) {
        // read the sub name
        tok = strtok_r(rest," \t",&rest);
        //TODO: assert tok != NULL

        /* printf("i go sub to %s\n",tok); */

        push_call(state.lineptr);

        struct subroutine_info info = get_subroutine_info(tok);
        //TODO: assert info.start != 0 && info.end != 0

        state.lineptr = info.start-1; // do -1 to counteract lineptr++ at the end of each line
    } else
    if (!strcmp(tok,W_RETURN)) {
        state.lineptr = pop_call(); // DONT -1 bc that would run `gosub` again and cause an infinite loop

        /* printf("returning to %d\n",state.lineptr); */
    }
}

void do_skip(char* tok) {
    // check if the current token affects the current block
    block_item* block = get_block();
    /* printf("current block: %d\n",block->type); */

    switch (block->type) {
    case BLOCK_IF:
        if (!strcmp(tok,W_IF)) {
            state.skip_depth++;
        } else if (state.skip_depth == 0 && !strcmp(tok,W_ELSE)) {
            // if there's an `else`, and we're already skipping, then stop skipping
            state.skip = false;
        } else if (!strcmp(tok,W_ENDIF)) { // if skip depth is 0 then it will stop skipping at the end of this func
            state.skip_depth--;
        }
        break;
    default:
        printf("gah! idk what to do! (do_skip)\n");
        break;
    }

    if (state.skip_depth == -1) { // -1 means that *this* token ended the skip state
        state.skip = false;
    }
}

void do_subroutine_line(char *line) {
    if (!strcmp(line,W_ENDSUB)) {
        add_subroutine(
            state.current_subroutine.name,
            (struct subroutine_info){
                .start = state.current_subroutine.start,
                .end = state.lineptr-1,
            }
        );
        state.current_subroutine.name = NULL; //invalidate current_subroutine
    }
}

void look_at_dis_line(char *str) {
    char line[strlen(str)+1];
    strcpy(line,str);

    if (state.current_subroutine.name != NULL) {
        do_subroutine_line(line);
        return;
    }

    char *tok;
    char *rest = line;

    // get keyword
    tok = strtok_r(rest, " \t", &rest);
    // (wont be NULL bc this doesnt get ran on empty strings)

    if (state.skip) {
        do_skip(tok);
        return;
    }

    /* printf("tok: %s\n",tok); */

    // is it an instruction?
    int inst = get_instruction(tok);
    if (inst != -1) { // if so then run it
        // trim whitespace
        /* while(isspace(*str)) str++; */
        // run
        do_instruction(tok,rest);
        return;
    }

    if (!strcmp(tok,W_IF)) {
        push_block(NEW_BLOCK_IF);
        /* printf("im in if!\n"); */
    } else
    if (!strcmp(tok,W_THEN)) {
        /* printf("then what\n"); */
        /* bool value = rand()&1; //TODO: actually get value */
        bool value = is_value_true(pop_val());
        /* printf("ok its %s\n",value?"true":"false"); */
        block_item* block = get_block();

        if (value == false) {
            state.skip = true;
            state.skip_depth = 0;
        }
    } else
    if (!strcmp(tok,W_ELSE)) {
        // if we see this then it means that we aren't skipping (so the
        // condition was true) so we should skip until `endif`
        state.skip = true;
        state.skip_depth = 0;
    } else
    if (!strcmp(tok,W_ENDIF)) {
        block_item block = pop_block();
        /* printf("just pooped block type %d i hope its %d\n", block.type, BLOCK_IF); */
    } else
    if (!strcmp(tok,W_DEFSUB)) { // start reading the subroutine
        tok = strtok_r(rest, " \t", &rest);
        if (tok == NULL) {
            printf("gah! expected a name for da subroutine\n");
            exit(1);
        }
        state.current_subroutine.name = strdup(tok);
        state.current_subroutine.start = state.lineptr+1;
    }

}

void interp_go() {
    for (;state.lineptr < state.linecount; state.lineptr++) {
        if (debug_lines) {
            printf("lp=%02d  |  '%s'\n",state.lineptr,state.lines[state.lineptr]);
        }
        if (strlen(state.lines[state.lineptr])==0) {
            /* printf("\tempty, skipping...\n"); */
            continue;
        }
        look_at_dis_line(state.lines[state.lineptr]);
    }
}

int countlines(FILE* f) {
    int count = 0;
    for (char c = getc(f); c != EOF; c = getc(f))
        if (c=='\n')
            count++;
    rewind(f);
    return count;
}

int main(int argc, char **argv) {
    srand(time(NULL));
    char* filename = NULL;
    
    // parse args
    for (int i=1;i<argc;i++) {
        if (!strcmp(argv[i],"-f")) {
            filename=argv[i+1];
            i++;
        } else if (!strcmp(argv[i],"-d")) {
            debug_pushes=true;
            debug_pops=true;
            debug_lines=true;
        }
    }

    if (filename==NULL) {
        printf("need filename (-f name)\n");
        exit(1);
    }

    FILE* f = fopen(filename,"r");
    if (f == NULL) {
        perror("fopen()");
        return 1;
    }

    state.lines = calloc(countlines(f), sizeof(char*));

    char *line_buf = NULL;
    size_t line_buf_size = 0;
    int line_size = 0;

    line_size = getline(&line_buf, &line_buf_size, f);

    while (line_size >= 0) {
        char* s = strdup(line_buf);
        s[strlen(s)-1] = 0;
        state.lines[state.linecount] = s;
        state.linecount++;

        line_size = getline(&line_buf, &line_buf_size, f);
    }

    free(line_buf);

    printf("%d lines\n",state.linecount);
    /* for (int i=0;i<state.linecount;i++) */
        /* printf("\t'%s'\n",state.lines[i]); */

    interp_go();

    fclose(f);

}

