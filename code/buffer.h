/*
 * Produtores colocam itens num buffer de tamanho fixo
 * e consumidores retiram. Duas restrições de sincronização:
 *   1) exclusão mútua: só uma thread mexe nos índices por vez;
 *   2) sincronização condicional: produtor espera se o buffer está CHEIO,
 *      consumidor espera se está VAZIO.
 *
 * Buffer circular com cap = 4
 *
 *        out           in
 *         ↓            ↓
 *      [ X ][ X ][ X ][   ]      itens em out..in-1; avança com (i + 1) % cap
 *
 *   - produtor escreve em buf[in]  e faz in  = (in  + 1) % cap
 *   - consumidor lê   de buf[out] e faz out = (out + 1) % cap
 *   - quando "in" chega ao fim do vetor, volta para 0 — por isso "circular".
 *
 * Duas versões:
 *   - buffer_a.c : VERSÃO A — 1 mutex + 2 variáveis de condição
 *   - buffer_b.c : VERSÃO B — 2 semáforos contadores + 1 mutex
 *   - buffer.c   : aloca o vetor e despacha para A ou B conforme b->versao
 *
*/

#ifndef BUFFER_H
#define BUFFER_H

#include <stdint.h>
#include "platform_thread.h"

typedef enum { VERSAO_A = 0, VERSAO_B = 1 } versao_t;

/* O que trafega no buffer. 16 bytes. */
typedef struct {
    uint64_t seq;   /* número sequencial 1..total, permite o CHECKSUM (nenhum item perdido/duplicado) */
    uint64_t ts;    /* pt_now_ns() no momento da produção, permite medir a latência */
} item_t;

typedef struct {
    versao_t versao;
    item_t  *buf;    /* vetor alocado com cap posições */
    int      cap;    /* capacidade: 1, 16 ou 256 */
    int      in;     /* próxima posição de escrita */
    int      out;    /* próxima posição de leitura */
    int      count;  /* a versão A usa para ver quantos itens há no buffer;
                        na versão B quem "conta" são os semáforos */

    pt_mutex_t m;

    /* versão A: as duas "condições" pelas quais uma thread pode esperar */
    pt_cond_t nao_cheio;   /* produtores esperam aqui quando count == cap */
    pt_cond_t nao_vazio;   /* consumidores esperam aqui quando count == 0 */

    /* versão B: dois contadores de recursos */
    pt_sem_t vagas;  /* posições livres  — começa em cap (buffer vazio) */
    pt_sem_t itens;  /* itens disponíveis — começa em 0  */
} buffer_t;

/* Interface usada pelo benchmark e pelos testes */
int    buffer_init(buffer_t *b, int cap, versao_t v);
void   buffer_put(buffer_t *b, item_t it);   /* bloqueia se cheio */
item_t buffer_get(buffer_t *b);              /* bloqueia se vazio */
void   buffer_destroy(buffer_t *b);

/* Implementações de cada versão (chamadas por buffer.c) */
int    bufa_init(buffer_t *b);
void   bufa_put(buffer_t *b, item_t it);
item_t bufa_get(buffer_t *b);
void   bufa_destroy(buffer_t *b);

int    bufb_init(buffer_t *b);
void   bufb_put(buffer_t *b, item_t it);
item_t bufb_get(buffer_t *b);
void   bufb_destroy(buffer_t *b);

#endif //BUFFER_H
