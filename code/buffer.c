/*
* buffer.c — parte comum do buffer: aloca o vetor e "despacha" cada
 * operação para a versão A ou B.
 *
 * Por que um despachante? Para o bench.c e os testes chamarem sempre
 * buffer_put/buffer_get, sem saber qual versão estão usando. A escolha é
 * um simples if por operação.
 */
#include <stdlib.h>
#include "buffer.h"

int buffer_init(buffer_t *b, int cap, versao_t v)
{
    b->versao = v;
    b->cap = cap;
    b->in = b->out = b->count = 0;               /* buffer começa vazio */
    b->buf = (item_t *)malloc(sizeof(item_t) * (size_t)cap);
    if (!b->buf) return -1;
    /* cada versão inicializa as proprias primitivas de sincronização */
    return (v == VERSAO_A) ? bufa_init(b) : bufb_init(b);
}

void buffer_put(buffer_t *b, item_t it)
{
    if (b->versao == VERSAO_A) bufa_put(b, it); else bufb_put(b, it);
}

item_t buffer_get(buffer_t *b)
{
    return (b->versao == VERSAO_A) ? bufa_get(b) : bufb_get(b);
}

void buffer_destroy(buffer_t *b)
{
    /* Só pode ser chamado depois que todas as threads terminaram (join):
     * destruir um mutex/semáforo com alguém esperando nele é comportamento
     * indefinido. */
    if (b->versao == VERSAO_A) bufa_destroy(b); else bufb_destroy(b);
    free(b->buf);
    b->buf = NULL;
}
