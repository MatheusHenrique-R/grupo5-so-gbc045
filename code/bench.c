/*
 * bench.c — programa de benchmark do Tema 2
 * ============================================================================
 *
 * UMA EXECUÇÃO DO PROGRAMA = UMA REPETIÇÃO DE UMA CONFIGURAÇÃO.
 * Quem repete 30 vezes e varia as configurações são os scripts em
 * experimentos/ (run_linux.sh e run_windows.ps1). Rodar cada repetição num
 * processo novo evita que uma execução "contamine" a seguinte (memória,
 * caches do alocador etc.).
 *
 * Uso:
 *   bench <so> <versao A|B> <produtores> <consumidores> <buffer> <total_itens> <rep> [arq_latencia]
 *
 *   ex.: bench linux A 4 4 16 400000 7
 *        bench windows B 1 1 1 400000 3 dados\windows_latencia.csv
 *
 * Saída (stdout): UMA linha CSV, sem cabeçalho (o script escreve o cabeçalho):
 *   so,versao,prod,cons,buf,total,rep,tempo_s,vazao,checksum_ok,user_s,kernel_s
 *
 * Latência (só se arq_latencia for passado E a configuração for 1×1 com
 * buffer 1, como pede o enunciado): acrescenta linhas no arquivo
 *   so,versao,rep,amostra,lat_ns
 * com 1 amostra a cada AMOSTRA_LAT itens.
 *
 * Código de saída: 0 = ok; 1 = argumentos inválidos/erro; 2 = checksum falhou.
 *
 * FLUXO DO main():
 *   1. lê e valida os argumentos
 *   2. inicializa o buffer da versão escolhida
 *   3. lê relógio e tempo de CPU  ─┐
 *   4. cria C consumidores e P produtores
 *   5. espera todos terminarem (join)        │ intervalo medido
 *   6. lê relógio e tempo de CPU  ─┘
 *   7. confere o checksum, imprime a linha CSV e grava as latências
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>   /* PRIu64: jeito portátil de imprimir uint64_t (MSVC, MinGW, GCC) */
#include "platform_thread.h"
#include "buffer.h"

#define AMOSTRA_LAT 100     /* grava a latência de 1 a cada 100 itens */
#define MAX_THREADS 64

/* ------------------------------------------------------------------------
 * Argumentos de cada thread. Cada thread recebe um ponteiro para a SUA
 * struct (posições diferentes de um vetor), então não há compartilhamento
 * entre threads aqui — só o buffer é compartilhado.
 * ------------------------------------------------------------------------ */
typedef struct {
    buffer_t *b;
    uint64_t  primeiro_seq;   /* este produtor gera seq = primeiro_seq .. primeiro_seq+quantidade-1 */
    uint64_t  quantidade;
} arg_produtor_t;

typedef struct {
    buffer_t *b;
    uint64_t  quantidade;     /* quantos itens este consumidor deve retirar */
    uint64_t  soma;           /* SAÍDA: soma dos seq retirados (para o checksum) */
    uint64_t *lat;            /* vetor de amostras de latência (NULL = não medir) */
    size_t    n_lat;          /* SAÍDA: quantas amostras foram gravadas */
} arg_consumidor_t;

/* ------------------------------------------------------------------------
 * PRODUTOR: coloca 'quantidade' itens no buffer, cada um com seu número
 * de sequência e o instante de produção.
 * ------------------------------------------------------------------------ */
static void *produtor(void *p)
{
    arg_produtor_t *a = (arg_produtor_t *)p;
    for (uint64_t i = 0; i < a->quantidade; i++) {
        item_t it;
        it.seq = a->primeiro_seq + i;
        it.ts  = pt_now_ns();          /* carimbo: "nasceu" agora */
        buffer_put(a->b, it);          /* bloqueia se o buffer estiver cheio */
    }
    return NULL;
}

/* ------------------------------------------------------------------------
 * CONSUMIDOR: retira 'quantidade' itens, soma os seq e (opcionalmente)
 * mede quanto tempo o item levou do produtor até aqui.
 * ------------------------------------------------------------------------ */
static void *consumidor(void *p)
{
    arg_consumidor_t *a = (arg_consumidor_t *)p;

    /* Soma numa variável LOCAL e só escreve em a->soma no final. Se
     * somássemos direto em a->soma, as structs dos consumidores (vizinhas
     * no vetor) poderiam cair na mesma linha de cache e as threads ficariam
     * "brigando" pela linha a cada item ("false sharing"), distorcendo a medida. */
    uint64_t soma = 0;
    size_t n_lat = 0;

    for (uint64_t i = 0; i < a->quantidade; i++) {
        item_t it = buffer_get(a->b);  /* bloqueia se o buffer estiver vazio */
        soma += it.seq;
        if (a->lat && (it.seq % AMOSTRA_LAT) == 0) {
            /* Latência = agora − instante em que o produtor criou o item.
             * Só lemos o relógio nas amostras para não pesar no laço. */
            a->lat[n_lat++] = pt_now_ns() - it.ts;
        }
    }
    a->soma  = soma;
    a->n_lat = n_lat;
    return NULL;
}

/* ------------------------------------------------------------------------
 * Utilitários de linha de comando
 * ------------------------------------------------------------------------ */
static void uso(const char *prog)
{
    fprintf(stderr,
        "uso: %s <so> <versao A|B> <produtores> <consumidores> <buffer> <total_itens> <rep> [arq_latencia]\n"
        "ex.: %s linux A 4 4 16 400000 1\n", prog, prog);
}

/* Converte texto em inteiro e confere se é um número válido dentro de [min, max].
 * strtoull indica em 'fim' onde parou de ler: se não parou no '\0', havia lixo
 * (ex.: "16abc"). */
static int ler_inteiro(const char *txt, uint64_t min, uint64_t max, uint64_t *saida)
{
    char *fim;
    unsigned long long v;
    if (txt[0] == '-') return -1;                /* strtoull aceitaria "-1"... */
    v = strtoull(txt, &fim, 10);
    if (*txt == '\0' || *fim != '\0' || v < min || v > max) return -1;
    *saida = (uint64_t)v;
    return 0;
}

/* ======================================================================== */
int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--help") == 0) { uso(argv[0]); return 0; }
    if (argc != 8 && argc != 9)                       { uso(argv[0]); return 1; }

    /* ---------------- 1. argumentos ---------------- */
    const char *so = argv[1];
    const char *arq_lat = (argc == 9) ? argv[8] : NULL;
    versao_t versao;
    uint64_t P, C, cap, total, rep;

    if      (strcmp(argv[2], "A") == 0) versao = VERSAO_A;
    else if (strcmp(argv[2], "B") == 0) versao = VERSAO_B;
    else { fprintf(stderr, "versao deve ser A ou B\n"); return 1; }

    if (ler_inteiro(argv[3], 1, MAX_THREADS, &P)       ||
        ler_inteiro(argv[4], 1, MAX_THREADS, &C)       ||
        ler_inteiro(argv[5], 1, 1000000, &cap)         ||
        ler_inteiro(argv[6], 1, 100000000, &total)     ||
        ler_inteiro(argv[7], 0, 1000000, &rep)) {
        fprintf(stderr, "argumento numerico invalido\n");
        uso(argv[0]);
        return 1;
    }

    /* CRITÉRIO DE TÉRMINO (exigido pelo enunciado): cada produtor produz
     * exatamente total/P itens e cada consumidor consome exatamente total/C.
     * Como a soma das duas é igual a total, todo item produzido é consumido
     * e toda espera termina — nenhuma thread fica bloqueada para sempre.
     * Para isso a divisão precisa ser exata. */
    if (total % P != 0 || total % C != 0) {
        fprintf(stderr, "total (%" PRIu64 ") deve ser divisivel por P e por C\n", total);
        return 1;
    }

    /* Mede latência só no cenário pedido pelo enunciado: 1×1, buffer 1. */
    int mede_lat = (arq_lat != NULL && P == 1 && C == 1 && cap == 1);

    /* ---------------- 2. preparação ---------------- */
    buffer_t b;
    if (buffer_init(&b, (int)cap, versao) != 0) {
        fprintf(stderr, "falha ao inicializar o buffer\n");
        return 1;
    }

    pt_thread_t      tp[MAX_THREADS], tc[MAX_THREADS];
    arg_produtor_t   ap[MAX_THREADS];
    arg_consumidor_t ac[MAX_THREADS];

    for (uint64_t i = 0; i < P; i++) {
        ap[i].b = &b;
        ap[i].quantidade   = total / P;
        ap[i].primeiro_seq = i * (total / P) + 1;   /* produtor 0: 1..N/P, produtor 1: N/P+1.. etc. */
    }
    for (uint64_t i = 0; i < C; i++) {
        ac[i].b = &b;
        ac[i].quantidade = total / C;
        ac[i].soma = 0;
        ac[i].n_lat = 0;
        ac[i].lat = NULL;
        if (mede_lat) {
            /* Alocado ANTES de começar a medir: malloc dentro do laço medido
             * atrapalharia o tempo. */
            ac[i].lat = (uint64_t *)malloc(sizeof(uint64_t) * (size_t)(total / C / AMOSTRA_LAT + 1));
            if (!ac[i].lat) { fprintf(stderr, "sem memoria\n"); return 1; }
        }
    }

    /* ---------------- 3–6. intervalo medido ---------------- */
    double u0, k0, u1, k1;
    pt_cpu_times(&u0, &k0);
    uint64_t t0 = pt_now_ns();

    /* Consumidores primeiro: eles já ficam esperando itens. A ordem não
     * altera a correção — só evita que os produtores comecem sozinhos. */
    for (uint64_t i = 0; i < C; i++)
        if (pt_thread_create(&tc[i], consumidor, &ac[i]) != 0) {
            fprintf(stderr, "falha ao criar consumidor\n"); return 1;
        }
    for (uint64_t i = 0; i < P; i++)
        if (pt_thread_create(&tp[i], produtor, &ap[i]) != 0) {
            fprintf(stderr, "falha ao criar produtor\n"); return 1;
        }

    for (uint64_t i = 0; i < P; i++) pt_thread_join(tp[i]);
    for (uint64_t i = 0; i < C; i++) pt_thread_join(tc[i]);

    uint64_t t1 = pt_now_ns();
    pt_cpu_times(&u1, &k1);

    /* ---------------- 7. resultados ---------------- */

    /* CHECKSUM: se cada número 1..total foi consumido exatamente uma vez, a
     * soma é a fórmula de Gauss total*(total+1)/2. Item perdido, duplicado
     * ou sobrescrito (bug de sincronização) muda a soma. Cabe em 64 bits
     * com folga (para total = 1e6, a soma é 5e11). */
    uint64_t soma = 0;
    for (uint64_t i = 0; i < C; i++) soma += ac[i].soma;
    uint64_t esperado = total * (total + 1) / 2;
    int checksum_ok = (soma == esperado);

    double tempo_s = (double)(t1 - t0) / 1e9;
    double vazao   = (double)total / tempo_s;      /* itens por segundo */

    printf("%s,%s,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64
           ",%.6f,%.0f,%d,%.6f,%.6f\n",
           so, (versao == VERSAO_A) ? "A" : "B", P, C, cap, total, rep,
           tempo_s, vazao, checksum_ok, u1 - u0, k1 - k0);
    fflush(stdout);

    if (mede_lat) {
        /* "a" = append: as 30 repetições vão se acumulando no mesmo arquivo
         * (o script cria o arquivo com o cabeçalho antes da 1ª execução). */
        FILE *f = fopen(arq_lat, "a");
        if (!f) { fprintf(stderr, "nao consegui abrir %s\n", arq_lat); return 1; }
        for (size_t j = 0; j < ac[0].n_lat; j++)
            fprintf(f, "%s,%s,%" PRIu64 ",%u,%" PRIu64 "\n",
                    so, (versao == VERSAO_A) ? "A" : "B", rep, (unsigned)j, ac[0].lat[j]);
        fclose(f);
    }

    for (uint64_t i = 0; i < C; i++) free(ac[i].lat);
    buffer_destroy(&b);

    if (!checksum_ok)
        fprintf(stderr, "ERRO: checksum %" PRIu64 " != esperado %" PRIu64 "\n", soma, esperado);
    return checksum_ok ? 0 : 2;
}
