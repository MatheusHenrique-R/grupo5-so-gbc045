#include <stdio.h>

#include "../code/platform_thread.h"

static int falhas = 0;

// usada para os nossos testes, imprime ok/falha e conta as falhas
#define CHECK(cond, msg)                                            \
    do{                                                             \
        if(cond){                                                   \
            printf("    [ok]    %s\n", msg);                        \
        }else{                                                      \
            printf("    [FALHA] %s (linha %d)\n", msg, __LINE__);   \
            falhas ++;                                              \
        }                                                           \
    }while (0)                                                      \

// cabeçalho de cada teste
#define INICIO(txt) do { printf("\n%s\n", txt); fflush(stdout); } while (0)

// trabalho inútil para ocupar a thread para testes
static void ocupar(long voltas){
    volatile long x = 0;
    for (long i = 0; i < voltas; i++) x += i;
}

// Teste 1: pt_thread_create / pt_thread_join
#define T1_THREADS 4

static void *t1_fn(void *arg){
    int *slot = (int *)arg;
    ocupar(2000000);
    *slot = 42;
    return NULL;
}

static void teste_thread(void){
    INICIO("[1] thread: create/join");

    pt_thread_t t[T1_THREADS];
    int resultado[T1_THREADS] = {0};
    int criou = 1, juntou = 1, todos = 1;

    for (int i = 0; i < T1_THREADS; i++)
        if(pt_thread_create(&t[i], t1_fn, &resultado[i]) != 0) criou = 0;

    for (int i = 0; i < T1_THREADS; i++)
        if(pt_thread_join(t[i]) != 0) juntou = 0;

    for (int i = 0; i < T1_THREADS; i++)
        if (resultado[i] != 42) todos = 0;

    CHECK(criou,  "pt_threads_create passou nas 4 threads");
    CHECK(juntou, "pt_thread_join passou nas 4 threads");
    CHECK(todos,  "cada thread recebeu seu argumento e gravou o resultado");
}

// Teste 2: pt_mutex_* (todas as operações de exclusão mútua)
#define T2_THREADS 4
#define T2_INCR    100000

static pt_mutex_t m2;
static long       contador2 = 0;

static void *t2_fn(void *arg){
    (void)arg;
    for(int i = 0; i < T2_INCR; i++){
        pt_mutex_lock(&m2);
        contador2++;
        pt_mutex_unlock(&m2);
    }
    return NULL;
}

static void teste_mutex(void){
    INICIO("[2] mutex: exclusao mutua");

    pt_thread_t t[T2_THREADS];

    CHECK(pt_mutex_init(&m2) == 0, "pt_mutex_init retornou ok");

    for(int i = 0; i < T2_THREADS; i++) pt_thread_create(&t[i], t2_fn, NULL);
    for(int i = 0; i < T2_THREADS; i++) pt_thread_join(t[i]);

    CHECK(contador2 == (long)T2_THREADS * T2_INCR,
          "contador final = 4 x 100000 (nenhum incremento perdido)");
    CHECK(pt_mutex_destroy(&m2) == 0, "pt_mutex_destroy retornou ok");
}

// Teste 3: pt_cond_wait / pt_cond_signal
static pt_mutex_t m3;
static pt_cond_t  c3;
static int        pronto3 = 0;

static void *t3_fn(void *arg){
    (void)arg;
    ocupar(2000000);    // faz a main esperar
    pt_mutex_lock(&m3);
    pronto3 = 1;
    pt_cond_signal(&c3);
    pt_mutex_unlock(&m3);
    return NULL;
}

static void teste_cond_signal(void){
    INICIO("[3] condicao: wait/signal");

    pt_thread_t t;
    CHECK(pt_mutex_init(&m3) == 0, "pt_mutex_init retornou ok");
    CHECK(pt_cond_init(&c3)  == 0, "pt_cond_init retornou ok");

    pt_thread_create(&t, t3_fn, NULL);

    pt_mutex_lock(&m3);
    while(!pronto3)
        pt_cond_wait(&c3, &m3);
    pt_mutex_unlock(&m3);

    pt_thread_join(t);

    CHECK(pronto3 == 1, "main acordou depois do signal e viu a flag ligada");
    pt_mutex_destroy(&m3);
}

// Teste 4: pt_cond_broadcast
#define T4_THREADS 4

static pt_mutex_t m4;
static pt_cond_t  c4_libera;    //threads dormem aqui
static pt_cond_t  c4_chegou;    //threads avisam a main que chegaram
static int        esperando4 = 0;
static int        liberado4  = 0;
static int        acordados4 = 0;

static void *t4_fn(void *arg){
    (void)arg;
    pt_mutex_lock(&m4);
    esperando4++;
    pt_cond_signal(&c4_chegou);
    while(!liberado4)
        pt_cond_wait(&c4_libera, &m4);
    acordados4++;
    pt_mutex_unlock(&m4);
    return NULL;
}

static void teste_cond_broadcast(void){
    INICIO("[4] condicao: broadcast");

    pt_thread_t t[T4_THREADS];
    pt_mutex_init(&m4);
    pt_cond_init(&c4_libera);
    pt_cond_init(&c4_chegou);

    for(int i = 0; i < T4_THREADS; i++) pt_thread_create(&t[i], t4_fn, NULL);

    pt_mutex_lock(&m4);
    while(esperando4 < T4_THREADS)
        pt_cond_wait(&c4_chegou, &m4);
    liberado4 = 1;
    CHECK(pt_cond_broadcast(&c4_libera) == 0, "pt_cond_broadcast retortou ok");
    pt_mutex_unlock(&m4);

    for(int i = 0; i < T4_THREADS; i++) pt_thread_join(t[i]);

    CHECK(acordados4 == T4_THREADS, "as 4 threads acordaram com 1 broadcast");
    pt_mutex_destroy(&m4);
}

// Teste 5: pt_now_ns
static void teste_tempo(void){
    INICIO("[5] tempo: pt_now_us");

    uint64_t anterior = pt_now_ns();
    uint64_t menor_passo = UINT64_MAX;
    int monotono = 1;

    for(int i = 0; i < 100000; i++){
        uint64_t agora = pt_now_ns();
        if(agora < anterior){
            monotono = 0;
        } else if (agora > anterior && agora - anterior < menor_passo){
            menor_passo = agora - anterior;
        }
        anterior = agora;
    }
    CHECK(monotono, "100000 leituras seguidas sem voltar no tempo");

    uint64_t t0 = pt_now_ns();
    ocupar(10000000);
    uint64_t t1 = pt_now_ns();

    CHECK(t1 > t0, "o relogio avanca durante um trabalho");
    CHECK(t1 - t0 < 10000000000ULL, "duração medida < 10 s (ordem de grandeza ok)");

    printf(" [info] trabalho de 10M de voltas: %llu ns\n",
           (unsigned long long)(t1 - t0));
    if(menor_passo != UINT64_MAX)
        printf(" [info] menor passo observado do relogio: %llu ns\n",
               (unsigned long long)menor_passo);

}


int main(void){
    printf("--- Testes da API_thread ---\n");

    teste_thread();
    teste_mutex();
    teste_cond_signal();
    teste_cond_broadcast();
    teste_tempo();

    printf("\n--- Resumo: %s (%d falha%s) ---",
           falhas == 0 ? "TUDO OK" : "HÁ FALHAS",
           falhas, falhas == 1 ? "": "s");

    return falhas == 0 ? 0 : 1;
}
