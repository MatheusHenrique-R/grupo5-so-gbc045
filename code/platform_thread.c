#define _POSIX_C_SOURCE 200809L

#include "./platform_thread.h"

// Windows
#ifdef _WIN32

#include <process.h> //usado na criação da thread
#include <stdlib.h>

// Struct usada para garantir a assinatura da API "void *f(void *)"
typedef struct{
    pt_thread_fn fn;
    void         *arg;
} tramp_t;

static unsigned __stdcall tramp(void *p){
    tramp_t dados = *(tramp_t *)p;
    free(p);

    dados.fn(dados.arg);
    return 0;
}

int pt_thread_create(pt_thread_t *t, pt_thread_fn fn, void *arg){
    tramp_t *p = (tramp_t *)malloc(sizeof *p);
    if (p == NULL) return -1;
    p->fn  = fn;
    p->arg = arg;

    // Não estamos usando o CreateThread pelo uso da biblioteca C
    uintptr_t h = _beginthreadex(NULL, 0, tramp, p, 0, NULL);
    if (h == 0){
        free(p);
        return -1;
    }
    *t = (pt_thread_t)h;
    return 0;
}

int pt_thread_join(pt_thread_t t){
    if(WaitForSingleObject(t, INFINITE) != WAIT_OBJECT_0) return -1;
    CloseHandle(t); // fechando o handle
    return 0;
}

// Mutex
int pt_mutex_init(pt_mutex_t *m){
    InitializeCriticalSection(m);
    return 0;
}
int pt_mutex_lock(pt_mutex_t *m){
    EnterCriticalSection(m);
    return 0;
}
int pt_mutex_unlock(pt_mutex_t *m){
    LeaveCriticalSection(m);
    return 0;
}
int pt_mutex_destroy(pt_mutex_t *m){
    DeleteCriticalSection(m);
    return 0;
}

// Variável de Condição
int pt_cond_init(pt_cond_t *c){
    InitializeConditionVariable(c);
    return 0;
}
int pt_cond_signal(pt_cond_t *c){
    WakeConditionVariable(c);
    return 0;
}
int pt_cond_broadcast(pt_cond_t *c){
    WakeAllConditionVariable(c);
    return 0;
}
int pt_cond_wait(pt_cond_t *c, pt_mutex_t *m){
    return SleepConditionVariableCS(c, m, INFINITE) ? 0 : -1;
}

// Semáforo
// O windows não tem um "sem_t". O semáforo é um objeto do kernel.
// Com isso, toda operação aqui passa pelo kernel, o que pode ser mais caro
int pt_sem_init(pt_sem_t *s, unsigned initial, unsigned max){
    *s = CreateSemaphore(NULL, (LONG)initial, (LONG)max, NULL);
    return (*s != NULL) ? 0 : -1;
}
int pt_sem_wait(pt_sem_t *s){
    return (WaitForSingleObject(*s, INFINITE) == WAIT_OBJECT_0) ? 0 : -1;
}
int pt_sem_post(pt_sem_t *s){
    return ReleaseSemaphore(*s, 1, NULL) ? 0 : -1;
}
int pt_sem_destroy(pt_sem_t *s){
    return CloseHandle(*s) ? 0 : -1;
}

// Tempo
uint64_t pt_now_ns(void){
    LARGE_INTEGER freq, ticks;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&ticks);

    uint64_t f   = (uint64_t)freq.QuadPart;
    uint64_t t   = (uint64_t)ticks.QuadPart;
    uint64_t seg = t/f;
    uint64_t res = t%f;

    return seg * 1000000000ULL + (res * 1000000000ULL) / f;
}
#else // linux

#include <time.h>
#include <errno.h>

int pt_thread_create(pt_thread_t *t, pt_thread_fn fn, void *arg){
    return pthread_create(t, NULL, fn, arg);
}
int pt_thread_join(pt_thread_t t){
    return pthread_join(t, NULL);
}

// Mutex
int pt_mutex_init(pt_mutex_t *m){
    return pthread_mutex_init(m, NULL);
}
int pt_mutex_lock(pt_mutex_t *m){
    return pthread_mutex_lock(m);
}
int pt_mutex_unlock(pt_mutex_t *m){
    return pthread_mutex_unlock(m);
}
int pt_mutex_destroy(pt_mutex_t *m){
    return pthread_mutex_destroy(m);
}

// Variavel de condição
int pt_cond_init(pt_cond_t *c){
    return pthread_cond_init(c, NULL);
}
int pt_cond_signal(pt_cond_t *c){
    return pthread_cond_signal(c);
}
int pt_cond_broadcast(pt_cond_t *c){
    return pthread_cond_broadcast(c);
}
int pt_cond_wait(pt_cond_t *c, pt_mutex_t *m){
    return pthread_cond_wait(c, m);
}

// Semáforo
int pt_sem_init(pt_sem_t *s, unsigned initial, unsigned max){
    (void)max;  //não usamos um teto no linux, somente no windows
    return sem_init(s, 0, initial);
}
int pt_sem_wait(pt_sem_t *s){
    // Aqui estamos fazendo um tratamento pois o sem_wait pode ser
    // interrompido por um sinal do SO e devolver -1, o que não é um erro
    // real no nosso cenário
    int r;
    while((r = sem_wait(s)) == -1 && errno == EINTR);
    return r;
}
int pt_sem_post(pt_sem_t *s){
    return sem_post(s);
}
int pt_sem_destroy(pt_sem_t *s){
    return sem_destroy(s);
}

// Tempo
uint64_t pt_now_ns(void){
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

#endif // _WIN32
