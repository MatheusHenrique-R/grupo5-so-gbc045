#include <stdio.h>
#include <stdlib.h>

#include "../code/platform_thread.h"

static int falhas = 0;

#define CHECK(cond, msg)                                            \
    do{                                                             \
        if(cond){                                                   \
            printf("    [ok]    %s\n", msg);                        \
        }else{                                                      \
            printf("    [FALHA] %s (linha %d)\n", msg, __LINE__);   \
            falhas++;                                               \
        }                                                           \
    }while(0)                                                       \

#define INICIO(txt) do { printf("\n%s\n", txt); fflush(stdout); } while (0)

// trabalho inútil para deixar a thread ocupada
static void ocupar(long voltas) {
    volatile long x = 0;
    for (long i = 0; i < voltas; i++) x += i;
}

// Teste 1: init / destroy
static void teste_init_destroy(void) {
    INICIO("[1] semaforo: init / destroy");

    pt_sem_t s;
    CHECK(pt_sem_init(&s, 0, 1) == 0,  "pt_sem_init retornou ok");
    CHECK(pt_sem_destroy(&s)    == 0,  "pt_sem_destroy retornou ok");
}

// Teste 2: contagem
static void teste_contagem(void) {
    INICIO("[2] semaforo: contagem (valor inicial e post/wait)");

    pt_sem_t s;
    pt_sem_init(&s, 3, 5);

    int w1 = pt_sem_wait(&s) == 0;
    int w2 = pt_sem_wait(&s) == 0;
    int w3 = pt_sem_wait(&s) == 0;
    CHECK(w1 && w2 && w3, "3 waits com valor inicial 3 passaram sem bloquear");

    int p1 = pt_sem_post(&s) == 0;
    int p2 = pt_sem_post(&s) == 0;
    CHECK(p1 && p2, "pt_sem_post retornou ok duas vezes");

    int w4 = pt_sem_wait(&s) == 0;
    int w5 = pt_sem_wait(&s) == 0;
    CHECK(w4 && w5, "2 waits apos 2 posts passaram sem bloquear");

    pt_sem_destroy(&s);
}

// Teste 3: wait realmente bloqueia até o post
static pt_sem_t s3;
static int      dado3 = 0;

static void *t3_fn(void *arg) {
    (void)arg;
    ocupar(5000000);       /* atrasa o post para a main chegar ao wait antes */
    dado3 = 99;
    pt_sem_post(&s3);
    return NULL;
}

static void teste_bloqueio(void) {
    INICIO("[3] semaforo: wait bloqueia ate o post");

    pt_thread_t t;
    pt_sem_init(&s3, 0, 1);

    pt_thread_create(&t, t3_fn, NULL);
    pt_sem_wait(&s3);                       /* tem que esperar a thread */
    int lido = dado3;
    pt_thread_join(t);

    CHECK(lido == 99, "main so passou do wait depois de a thread escrever e dar post");
    pt_sem_destroy(&s3);
}

// Teste 4: Semáforo binário como exclusão mutua
#define T4_THREADS 4
#define T4_INCR    100000

static pt_sem_t s4;
static long     contador4 = 0;

static void *t4_fn(void *arg) {
    (void)arg;
    for (int i = 0; i < T4_INCR; i++) {
        pt_sem_wait(&s4);
        contador4++;
        pt_sem_post(&s4);
    }
    return NULL;
}

static void teste_binario(void) {
    INICIO("[4] semaforo binario: exclusao mutua");

    pt_thread_t t[T4_THREADS];
    pt_sem_init(&s4, 1, 1);

    for (int i = 0; i < T4_THREADS; i++) pt_thread_create(&t[i], t4_fn, NULL);
    for (int i = 0; i < T4_THREADS; i++) pt_thread_join(t[i]);

    CHECK(contador4 == (long)T4_THREADS * T4_INCR,
          "contador final = 4 x 100000 (nenhum incremento perdido)");
    pt_sem_destroy(&s4);
}

// Teste 5: semáforo contador limitando concorrência
#define T5_THREADS 6
#define T5_LIMITE  2
#define T5_VOLTAS  200

static pt_sem_t   s5;
static pt_mutex_t m5;
static int        dentro5 = 0;
static int        maximo5 = 0;

static void *t5_fn(void *arg) {
    (void)arg;
    for (int i = 0; i < T5_VOLTAS; i++) {
        pt_sem_wait(&s5);

        pt_mutex_lock(&m5);
        dentro5++;
        if (dentro5 > maximo5) maximo5 = dentro5;
        pt_mutex_unlock(&m5);

        ocupar(2000);               /* fica "dentro" um instante */

        pt_mutex_lock(&m5);
        dentro5--;
        pt_mutex_unlock(&m5);

        pt_sem_post(&s5);
    }
    return NULL;
}

static void teste_limite(void) {
    INICIO("[5] semaforo contador: limite de concorrencia");

    pt_thread_t t[T5_THREADS];
    pt_mutex_init(&m5);
    pt_sem_init(&s5, T5_LIMITE, T5_LIMITE);

    for (int i = 0; i < T5_THREADS; i++) pt_thread_create(&t[i], t5_fn, NULL);
    for (int i = 0; i < T5_THREADS; i++) pt_thread_join(t[i]);

    CHECK(maximo5 <= T5_LIMITE, "nunca houve mais de 2 threads dentro ao mesmo tempo");
    CHECK(maximo5 >= 1,         "pelo menos 1 thread entrou (o teste exercitou o semaforo)");
    CHECK(dentro5 == 0,         "no final nenhuma thread ficou dentro");
    printf("  [info]  maximo de threads dentro ao mesmo tempo: %d\n", maximo5);

    pt_sem_destroy(&s5);
    pt_mutex_destroy(&m5);
}

// Teste 6: Versão B em miniatura
typedef struct {
    int         *dados;
    int          tamanho;
    int          entrada;      /* proximo indice de escrita */
    int          saida;        /* proximo indice de leitura */
    pt_mutex_t   mutex;        /* protege SO os indices      */
    pt_sem_t     vagas;        /* vagas livres  (comeca em tamanho) */
    pt_sem_t     itens;        /* itens prontos (comeca em 0)       */
} fila_sem_t;

typedef struct {
    fila_sem_t *f;
    int         primeiro;      /* primeiro valor a produzir (produtor)   */
    int         quantidade;    /* quantos itens produzir / consumir      */
    long long   soma;          /* soma dos valores consumidos (consumidor) */
    int         em_ordem;      /* 1 se os valores vieram crescentes      */
} tarefa_t;

static void fila_init(fila_sem_t *f, int tamanho) {
    f->dados   = (int *)calloc((size_t)tamanho, sizeof(int));
    f->tamanho = tamanho;
    f->entrada = 0;
    f->saida   = 0;
    pt_mutex_init(&f->mutex);
    pt_sem_init(&f->vagas, (unsigned)tamanho, (unsigned)tamanho);
    pt_sem_init(&f->itens, 0,                 (unsigned)tamanho);
}

static void fila_destroy(fila_sem_t *f) {
    pt_sem_destroy(&f->itens);
    pt_sem_destroy(&f->vagas);
    pt_mutex_destroy(&f->mutex);
    free(f->dados);
}

static void *produtor6(void *arg) {
    tarefa_t *t = (tarefa_t *)arg;
    fila_sem_t *f = t->f;
    for (int i = 0; i < t->quantidade; i++) {
        pt_sem_wait(&f->vagas);                 /* down(vagas)  */
        pt_mutex_lock(&f->mutex);
        f->dados[f->entrada] = t->primeiro + i;
        f->entrada = (f->entrada + 1) % f->tamanho;
        pt_mutex_unlock(&f->mutex);
        pt_sem_post(&f->itens);                 /* up(itens)    */
    }
    return NULL;
}

static void *consumidor6(void *arg) {
    tarefa_t *t = (tarefa_t *)arg;
    fila_sem_t *f = t->f;
    int anterior = 0;
    t->soma = 0;
    t->em_ordem = 1;
    for (int i = 0; i < t->quantidade; i++) {
        pt_sem_wait(&f->itens);                 /* down(itens)  */
        pt_mutex_lock(&f->mutex);
        int v = f->dados[f->saida];
        f->saida = (f->saida + 1) % f->tamanho;
        pt_mutex_unlock(&f->mutex);
        pt_sem_post(&f->vagas);                 /* up(vagas)    */

        if (v <= anterior) t->em_ordem = 0;
        anterior = v;
        t->soma += v;
    }
    return NULL;
}

/* Roda uma configuracao P x C com buffer "tam" e "total" itens no total.
 * "total" precisa ser divisivel por P e por C. */
static void roda_buffer(int P, int C, int tam, int total) {
    char msg[96];
    fila_sem_t f;
    pt_thread_t tp[4], tc[4];
    tarefa_t    ap[4], ac[4];

    fila_init(&f, tam);

    for (int i = 0; i < P; i++) {
        ap[i].f = &f;
        ap[i].quantidade = total / P;
        ap[i].primeiro   = i * (total / P) + 1;   /* faixas sem repeticao */
        pt_thread_create(&tp[i], produtor6, &ap[i]);
    }
    for (int i = 0; i < C; i++) {
        ac[i].f = &f;
        ac[i].quantidade = total / C;
        pt_thread_create(&tc[i], consumidor6, &ac[i]);
    }
    for (int i = 0; i < P; i++) pt_thread_join(tp[i]);
    for (int i = 0; i < C; i++) pt_thread_join(tc[i]);

    long long soma = 0;
    for (int i = 0; i < C; i++) soma += ac[i].soma;
    long long esperado = (long long)total * (total + 1) / 2;

    snprintf(msg, sizeof msg, "%dx%d, buffer %d: soma consumida == soma produzida",
             P, C, tam);
    CHECK(soma == esperado, msg);

    snprintf(msg, sizeof msg, "%dx%d, buffer %d: indices voltaram ao ponto inicial",
             P, C, tam);
    /* entrada e saida andaram "total" posicoes cada (modulo tamanho) */
    CHECK(f.entrada == f.saida, msg);

    if (P == 1 && C == 1) {
        snprintf(msg, sizeof msg, "%dx%d, buffer %d: itens sairam em ordem (FIFO)",
                 P, C, tam);
        CHECK(ac[0].em_ordem, msg);
    }

    fila_destroy(&f);
}

static void teste_buffer(void) {
    INICIO("[6] Versao B em miniatura: buffer circular com 2 semaforos");

    roda_buffer(1, 1,   1, 2000);   /* buffer minimo: forca alternancia */
    roda_buffer(1, 1,  16, 2000);
    roda_buffer(4, 4,   1, 2000);
    roda_buffer(4, 4,  16, 2000);
    roda_buffer(4, 4, 256, 2000);
}

int main(void) {
    printf("=== Testes do semaforo (pt_sem_*) ===\n");

    teste_init_destroy();
    teste_contagem();
    teste_bloqueio();
    teste_binario();
    teste_limite();
    teste_buffer();

    printf("\n=== Resumo: %s (%d falha%s) ===\n",
           falhas == 0 ? "TUDO OK" : "HA FALHAS",
           falhas, falhas == 1 ? "" : "s");
    return falhas == 0 ? 0 : 1;
}
