/*(MSC) Morphological Source Code Framework
 * =============================================================
 *  toruslang.c  —  cache-local syntactic bytecode + micro-JIT + LSP
 *  gcc -O3 -std=c99 toruslang.c -o toruslang
 *  toruslang --lsp   → pipe into clangd client
 *  toruslang --repl  → human REPL
 *  copyright: | Licenses: CC ND & BSD-3
 *  © 2025 Phovos https://github.com/Phovos/Morphological-Source-Code
 *  © 2023-2025 Moonlapsed https://github.com/MOONLAPSED/Cognosis
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <time.h>

/* ---------- 0.  CONFIG -------------------------------------------------- */
enum { LX = 256 , LY = 256 };
enum { BUF_LINES = 1024 , LINE_SZ = 256 };
typedef uint8_t num8_t;

/* ---------- 1.  ARENA --------------------------------------------------- */
typedef struct {
    char     text[LINE_SZ];
    uint32_t hash;
    uint8_t  bc[128];
    uint8_t  bc_len;
} line_t;
static line_t arena[BUF_LINES];
static int    last_line = 0;

/* ---------- 2.  LEDGER -------------------------------------------------- */
typedef struct { uint64_t landauer; } ledger_t;
static ledger_t ledger = {0};

/* ---------- 3.  NUMBER FORMAT (Q4.4 fixed) ------------------------------ */
static inline num8_t encode8(double x){
    int16_t q = (int16_t)(x*16.0 + 0.5);
    if(q>127) q=127; else if(q<-128) q=-128;
    return (uint8_t)(int8_t)q;
}
static inline double decode8(num8_t p){ return ((int8_t)p)/16.0; }
static inline num8_t add8(num8_t a,num8_t b,ledger_t *l){
    l->landauer++;
    int16_t r = (int16_t)(int8_t)a + (int16_t)(int8_t)b;
    if(r>127) r=127; else if(r<-128) r=-128;
    return (uint8_t)(int8_t)r;
}

/* ---------- 4.  BYTECODE VM -------------------------------------------- */
typedef enum {
    OP_POSIT_ADD = 0x01,
    OP_QUINE_MOMENT = 0x20,
    OP_HALT = 0xFF
} op_t;

static num8_t stack[256];
static int    sp = 0;

static void vm_reset(void){ sp = 0; }

static bool vm_step(uint8_t op){
    switch(op){
      case OP_POSIT_ADD:
        if(sp<2) return false;
        sp--;
        stack[sp-1] = add8(stack[sp-1],stack[sp],&ledger);
        break;
      case OP_QUINE_MOMENT:
        ledger.landauer++;
        break;
      case OP_HALT:
        return false;
      default:
        return false;
    }
    return true;
}

static void vm_run_line(const line_t *L){
    for(int i=0;i<L->bc_len;i++) if(!vm_step(L->bc[i])) break;
}

/* ---------- 5.  MICRO-JIT ---------------------------------------------- */
static uint32_t djb_hash(const char *s,size_t len){
    uint32_t h = 5381;
    while(len--) h = ((h<<5)+h) + (uint8_t)*s++;
    return h;
}
static void emit_byte(line_t *L,uint8_t b){
    if(L->bc_len<sizeof(L->bc)) L->bc[L->bc_len++] = b;
}
static void skip_space(const char **pp){ while(**pp==' '||**pp=='\t') (*pp)++; }

static void jit_line(int lineno){
    line_t *L = &arena[lineno];
    uint32_t old = L->hash;
    L->hash = djb_hash(L->text, strlen(L->text));
    if(L->hash == old) return;
    L->bc_len = 0;
    const char *p = L->text;
    while(*p){
        skip_space(&p);
        if(strncmp(p,"add",3)==0){ p+=3; emit_byte(L, OP_POSIT_ADD); }
        else if(strncmp(p,"quine",5)==0){ p+=5; emit_byte(L, OP_QUINE_MOMENT); }
        else break;
    }
}
static void patch_vm(void){
    vm_reset();
    for(int i=0;i<=last_line;i++) jit_line(i);
}

/* ---------- 6.  TINY LSP SERVER ---------------------------------------- */
static void lsp_loop(void){
    char buf[4096];
    while(fgets(buf,sizeof(buf),stdin)){
        /* minimal didSave → re-JIT */
        if(strstr(buf,"textDocument/didSave")) patch_vm();
        /* empty completion */
        if(strstr(buf,"textDocument/completion")){
            const char *resp = "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":[]}";
            printf("Content-Length: %zu\r\n\r\n%s\n", strlen(resp), resp);
            fflush(stdout);
        }
    }
}
/* ---------- 7.  REPL FOR HUMANS ---------------------------------------- */
static void repl(void){
    printf("toruslang 0.1  (type 'exit' to quit)\n");
    while(1){
        printf("> "); fflush(stdout);
        if(!fgets(arena[last_line].text, LINE_SZ, stdin)) break;
        if(strncmp(arena[last_line].text,"exit",4)==0) break;
        jit_line(last_line);
        vm_run_line(&arena[last_line]);
        printf("top=%.3f  landauer=%llu\n", decode8(stack[sp>0?sp-1:0]),
               (unsigned long long)ledger.landauer);
        last_line = (last_line+1) % BUF_LINES;
    }
}

/* -------- 9.  THERMO-FITNESS QUINE ------------------------------------ */
typedef struct {
    uint64_t birth_ns;
    uint64_t parent_ns;
    uint64_t landauer;
    double   energy_j;      // read via perf syscalls if available
    uint32_t cache_misses;
    uint32_t hash;
    uint8_t  bc[128];
    uint8_t  bc_len;
} quine_t;

static quine_t population[64];

static double fitness(const quine_t *q){
    double E = q->energy_j + 1e-12;
    double M = q->cache_misses + 1;
    double B = q->landauer + 1;
    return 1.0 / (E + 1e-6*M + 1e-9*B);
}

static void quine_breed(void){
    /* sort by fitness (descending) */
    qsort(population, 64, sizeof(quine_t),
          (int(*)(const void*,const void*))
          [](const quine_t *a,const quine_t *b){
              double fa = fitness(a), fb = fitness(b);
              return (fa>fb)?-1:(fa<fb)?1:0;
          });
    /* overwrite weakest 25 % with copy of strongest 25 % */
    for(int i=48;i<64;i++){
        int src = i-48;
        population[i] = population[src];
        population[i].birth_ns = now_ns();
        population[i].parent_ns = population[src].birth_ns;
        population[i].landauer = 0;   // reset toll
    }
}

/* ---------- 8.  MAIN SWITCH -------------------------------------------- */
int main(int argc,char **argv){
    if(argc==2 && strcmp(argv[1],"--lsp")==0){ lsp_loop(); return 0; }
    if(argc==2 && strcmp(argv[1],"--repl")==0){ repl();   return 0; }
    fprintf(stderr,"usage: %s --lsp | --repl\n", argv[0]);
    return 1;
}

/* -------------- SELF-HOSTED BUILD SUPPORT ------------------------------- */
#include <unistd.h>   /* getcwd */
#include <limits.h>   /* PATH_MAX */

static void print_compile_commands(void){
    char dir[PATH_MAX];
    if(!getcwd(dir,sizeof(dir))) return;
    printf("[\n  {\n");
    printf("    \"directory\": \"%s\",\n", dir);
    printf("    \"command\": \"clang -O3 -std=c99 -Wall -Wextra torus.c -o torus\",\n");
    printf("    \"file\": \"torus.c\"\n");
    printf("  }\n]\n");
}

static void print_help(void){
    printf("usage: ./torus [options]\n"
           "  --lsp   emit compile_commands.json for clangd\n"
           "  --run   run benchmark (default)\n");
}

int main(int argc,char **argv){
    if(argc==2){
        if(strcmp(argv[1],"--lsp")==0){ print_compile_commands(); return 0; }
        if(strcmp(argv[1],"--help")==0){ print_help(); return 0; }
    }
    /* else fall through to original benchmark */
    printf("LX=%d LY=%d  format=%s\n",LX,LY, USE_Q44 ? "Q4.4" : "posit-cheat");
    clock_t t0 = clock();
    double D = measure_diffusion();
    double ms = (double)(clock()-t0)*1000.0/CLOCKS_PER_SEC;
    printf("1e6 sweeps in %.1f ms  D=%.4f  landauer=%llu\n",
           ms, D, (unsigned long long)ledger.landauer);
    return 0;
}
