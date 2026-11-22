/*
 * test_buffer.c — testa as versões A e B do buffer em todas as combinações
 * ============================================================================
 * Para cada versão (A, B) × P×C (1×1, 4×4, 2×4) × buffer (1, 16, 256):
 *   - produtores geram os números 1..N (cada um uma faixa);
 *   - consumidores somam o que retiram e marcam num vetor "visto[seq]";
 *   - no fim: cada número tem que ter sido visto uma vez.
 *
 * Isso pega os bugs típicos:
 *   - item perdido (sobrescrito por if no lugar de while)  → visto == 0
 *   - item duplicado (dois consumidores leem a mesma posição) → visto == 2
 *   - deadlock (ordem errada sem_wait/lock)                → teste trava e o CTest acusa
 *
 * O caso 2×4 (P ≠ C) testa o critério de término com divisões diferentes.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../code/platform_thread.h"
#include "../code/buffer.h"

#define N 40000            /* divisível por 1, 2 e 4 */

static buffer_t b;
static unsigned char visto[N + 1];
static pt_mutex_t mv;      /* protege 'visto' */

typedef struct { uint64_t primeiro, qtd; } faixa_t;

static void *produtor(void *p)
{
    faixa_t *f = (faixa_t *)p;
    for (uint64_t i = 0; i < f->qtd; i++) {
        item_t it = { f->primeiro + i, pt_now_ns() };
        buffer_put(&b, it);
    }
    return NULL;
}

static void *consumidor(void *p)
{
    uint64_t qtd = *(uint64_t *)p;
    for (uint64_t i = 0; i < qtd; i++) {
        item_t it = buffer_get(&b);
        pt_mutex_lock(&mv);
        if (it.seq >= 1 && it.seq <= N) visto[it.seq]++;
        pt_mutex_unlock(&mv);
    }
    return NULL;
}

static int roda(versao_t v, int P, int C, int cap)
{
    pt_thread_t tp[4], tc[4];
    faixa_t f[4];
    uint64_t qc = N / C;

    memset(visto, 0, sizeof visto);
    if (buffer_init(&b, cap, v) != 0) return 1;

    for (int i = 0; i < C; i++) pt_thread_create(&tc[i], consumidor, &qc);
    for (int i = 0; i < P; i++) {
        f[i].qtd = N / P;
        f[i].primeiro = (uint64_t)i * (N / P) + 1;
        pt_thread_create(&tp[i], produtor, &f[i]);
    }
    for (int i = 0; i < P; i++) pt_thread_join(tp[i]);
    for (int i = 0; i < C; i++) pt_thread_join(tc[i]);
    buffer_destroy(&b);

    for (int s = 1; s <= N; s++)
        if (visto[s] != 1) {
            printf("Falha versao %c %dx%d buf %d: item %d visto %d vezes\n",
                   v == VERSAO_A ? 'A' : 'B', P, C, cap, s, visto[s]);
            return 1;
        }
    printf("ok  versao %c  %dx%d  buffer %3d\n", v == VERSAO_A ? 'A' : 'B', P, C, cap);
    return 0;
}

int main(void)
{
    int pcs[3][2] = { {1, 1}, {4, 4}, {2, 4} };
    int caps[3] = { 1, 16, 256 };
    int falhas = 0;

    /* Sem buffer no stdout: cada linha aparece na hora. Se o teste travar,
     * a última linha mostra até onde chegou (sem isso, a saída se perderia). */
    setvbuf(stdout, NULL, _IONBF, 0);

    pt_mutex_init(&mv);
    for (int v = 0; v < 2; v++)
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
                falhas += roda((versao_t)v, pcs[i][0], pcs[i][1], caps[j]);
    pt_mutex_destroy(&mv);

    if (falhas) { printf("FALHA: %d combinacoes com erro\n", falhas); return 1; }
    printf("OK\n");
    return 0;
}
