/*
 * buffer_b.c — VERSÃO B: 2 semáforos contadores + 1 mutex
 *
 * É a solução clássica de Dijkstra mostrada na Aula parte 2, com
 *     vagas = "empty"   (posições livres;    começa em cap)
 *     itens = "full"    (posições ocupadas;  começa em 0)
 *     m     = "mutex"
 *
 * Diferença de filosofia em relação à versão A: aqui o estado (quantas vagas
 * e quantos itens existem) mora dentro dos semáforos. Não há count, nem
 * while: o próprio sem_wait decrementa ou bloqueia, atomicamente. Um "up"
 * feito sem ninguém esperando não se perde — fica guardado no contador.
 *
 * O mutex protege só a manipulação de in/out e do vetor, porque com 4
 * produtores dois deles podem ter passado pelo sem_wait(vagas) ao mesmo
 * tempo e tentar escrever no mesmo buf[in].
 */
#include "buffer.h"

int bufb_init(buffer_t *b)
{
    if (pt_mutex_init(&b->m) != 0) return -1;
    if (pt_sem_init(&b->vagas, (unsigned)b->cap, (unsigned)b->cap) != 0) return -1;  /* tudo livre */
    if (pt_sem_init(&b->itens, 0, (unsigned)b->cap) != 0) return -1;                 /* nada para consumir */
    return 0;
}

void bufb_put(buffer_t *b, item_t it)
{
    // 1º: reserva uma vaga. Se não houver, dorme AQUI, sem segurar o mutex.

    pt_sem_wait(&b->vagas);
    pt_mutex_lock(&b->m);
    b->buf[b->in] = it;
    b->in = (b->in + 1) % b->cap;
    pt_mutex_unlock(&b->m);

    /* 2º: anuncia que há mais um item. Fora do mutex: não precisa dele e
     * assim a região crítica fica a menor possível. */
    pt_sem_post(&b->itens);
}

item_t bufb_get(buffer_t *b)
{
    item_t it;

    pt_sem_wait(&b->itens);          /* reserva um item (ou dorme até existir) */

    pt_mutex_lock(&b->m);
    it = b->buf[b->out];
    b->out = (b->out + 1) % b->cap;
    pt_mutex_unlock(&b->m);

    pt_sem_post(&b->vagas);          /* devolve a vaga */
    return it;
}

void bufb_destroy(buffer_t *b)
{
    pt_sem_destroy(&b->vagas);
    pt_sem_destroy(&b->itens);
    pt_mutex_destroy(&b->m);
}
