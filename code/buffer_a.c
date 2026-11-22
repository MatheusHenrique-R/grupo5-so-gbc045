/*
 * buffer_a.c — VERSÃO A: 1 mutex + 2 variáveis de condição
 *
 * Toda operação segue o mesmo roteiro:
 *
 *   1. trava o mutex                    exclusão mútua sobre buf, in, out, count
 *   2. ENQUANTO a condição não permite  espera na variável de condição
 *      (pt_cond_wait libera o mutex enquanto dorme e o retoma ao acordar)
 *   3. faz a operação e atualiza count
 *   4. sinaliza a OUTRA condição        → acorda quem pode ter sido destravado
 *   5. destrava o mutex
 *
 * Aqui o estado "cheio/vazio" é uma variável comum (count) protegida pelo
 * mutex. As variáveis de condição NÃO guardam estado: são só filas de espera.
 */

#include "buffer.h"

int bufa_init(buffer_t *b)
{
    if (pt_mutex_init(&b->m) != 0) return -1;
    if (pt_cond_init(&b->nao_cheio) != 0) return -1;
    if (pt_cond_init(&b->nao_vazio) != 0) return -1;
    return 0;
}

void bufa_put(buffer_t *b, item_t it)
{
    pt_mutex_lock(&b->m);

    /* WHILE, nunca IF. Dois motivos:
     *  (a) despertar espúrio: o SO pode acordar a thread sem nenhum signal;
     *  (b) no 4×4, entre o signal e o momento em que esta thread consegue
     *      retomar o mutex, OUTRO produtor pode ter ocupado a vaga.
     * Com while, ao acordar a thread confere a condição de novo e, se ainda
     * estiver cheio, volta a dormir. Com if, escreveria por cima de um item. */
    while (b->count == b->cap)
        pt_cond_wait(&b->nao_cheio, &b->m);

    /* Aqui é certo que há vaga e que temos o mutex. */
    b->buf[b->in] = it;
    b->in = (b->in + 1) % b->cap;     /* avança circularmente */
    b->count++;

    /* Agora o buffer certamente NÃO está vazio: acorda UM consumidor que
     * esteja esperando. Um signal basta, porque só entrou um
     * item — só um consumidor pode aproveitar. Sinalizar com o mutex ainda
     * travado é o jeito mais simples de raciocinar. */
    pt_cond_signal(&b->nao_vazio);

    pt_mutex_unlock(&b->m);
}

item_t bufa_get(buffer_t *b)
{
    item_t it;

    pt_mutex_lock(&b->m);

    while (b->count == 0)                    /* vazio → espera chegar item */
        pt_cond_wait(&b->nao_vazio, &b->m);

    it = b->buf[b->out];
    b->out = (b->out + 1) % b->cap;
    b->count--;

    pt_cond_signal(&b->nao_cheio);           /* abriu uma vaga → acorda um produtor */

    pt_mutex_unlock(&b->m);
    return it;
}

void bufa_destroy(buffer_t *b)
{
    pt_cond_destroy(&b->nao_cheio);
    pt_cond_destroy(&b->nao_vazio);
    pt_mutex_destroy(&b->m);
}
