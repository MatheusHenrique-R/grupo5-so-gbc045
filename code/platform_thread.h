#ifndef TRABALHO_PLATFORM_THREAD_H
#define TRABALHO_PLATFORM_THREAD_H

#include <stdint.h>

// Tratando a diferença de Sistemas Operacionais

#ifdef _WIN32 // API nativa do windows
    #include <windows.h>
    typedef HANDLE              pt_thread_t;    // "alça" para objeto thread do kernel
typedef CRITICAL_SECTION    pt_mutex_t;     // mutex de modo usuário
typedef CONDITION_VARIABLE  pt_cond_t;      // variável de condição de modo uruáior
typedef HANDLE              pt_sem_t;       // semáforo é objeto do kernel, acessível apenas por HANDLE
#else         // API nativa do linux
#include <pthread.h>
#include <semaphore.h>
typedef pthread_t           pt_thread_t;
typedef pthread_mutex_t     pt_mutex_t;
typedef pthread_cond_t      pt_cond_t;
typedef sem_t               pt_sem_t;
#endif

// Assinatura da função de thread para padronizar nos dois SOs
typedef void *(*pt_thread_fn)(void *);

// Importante: retorna 0 em caso de sucesso

// Thread
int pt_thread_create(pt_thread_t *t, pt_thread_fn fn, void *arg);
int pt_thread_join(pt_thread_t t);

// Mutex
int pt_mutex_init(pt_mutex_t *m);
int pt_mutex_lock(pt_mutex_t *m);
int pt_mutex_unlock(pt_mutex_t *m);
int pt_mutex_destroy(pt_mutex_t *m);

// Variável de condição
int pt_cond_init(pt_cond_t *c);
int pt_cond_wait(pt_cond_t *c, pt_mutex_t *m);
int pt_cond_signal(pt_cond_t *c);
int pt_cond_broadcast(pt_cond_t *c);
int pt_cond_destroy(pt_cond_t *c);

// Semáforo
int pt_sem_init(pt_sem_t *s, unsigned initial, unsigned max);
int pt_sem_wait(pt_sem_t *s);   // down
int pt_sem_post(pt_sem_t *s);   // up
int pt_sem_destroy(pt_sem_t *s);

// Tempo
uint64_t pt_now_ns(void);

#endif //TRABALHO_PLATFORM_THREAD_H
