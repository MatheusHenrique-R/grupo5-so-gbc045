/*
 * test_bench.c — testa o programa de benchmark (bench) "por fora"
 * ============================================================================
 * O bench tem o seu próprio main(), então não dá para chamá-lo como função.
 * Este teste executa o bench como outro processo (igual aos scripts de coleta
 * fazem), lê o que ele imprimiu e confere o código de saída.
 *
 * Como recebe o caminho do bench:
 *   - pelo CTest: test_bench <caminho_do_bench>   (o CMakeLists passa sozinho)
 *   - pelo botão ▶ do CLion (sem argumentos): procura "bench"/"bench.exe" na
 *     MESMA pasta onde está este executável de teste.
 *
 * O que é verificado:
 *   [1] as 12 configurações (A/B × 1×1, 4×4 × buffer 1, 16, 256):
 *       código 0, UMA linha CSV com 10 campos, campos iguais aos argumentos,
 *       checksum_ok = 1, tempo > 0 e vazão ≈ total / tempo
 *   [2] --help → código 0
 *   [3] argumentos inválidos → código 1 (sem args, versão "C", "16abc",
 *       número negativo, P = 0, total não divisível por P)
 *   [4] latência: 1×1 buffer 1 com arquivo → grava total/100 linhas válidas
 *   [5] latência: 4×4 buffer 16 com arquivo → NÃO grava nada
 */
#define _CRT_SECURE_NO_WARNINGS   /* MSVC: aceitar fopen/strcpy sem aviso */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
  #define popen  _popen           /* no Windows as funções têm "_" na frente */
  #define pclose _pclose
  #define SEM_STDERR " 2>NUL"     /* esconde as mensagens de erro esperadas */
#else
  #include <sys/wait.h>           /* WEXITSTATUS */
  #define SEM_STDERR " 2>/dev/null"
#endif

#define ARQ_LAT "test_bench_latencia.csv"

static int  falhas = 0;
static char bench[1024];            /* caminho do executável do bench */

#define CHECK(cond, ...) do {                         \
        if (cond) { printf("  [ok]    "); }           \
        else      { printf("  [FALHA] "); falhas++; } \
        printf(__VA_ARGS__); printf("\n");            \
    } while (0)

/* ------------------------------------------------------------------------
 * Executa: bench <args>. Guarda em 'saida' o que foi impresso no stdout e
 * devolve o código de saída do processo (0, 1, 2...).
 * ------------------------------------------------------------------------ */
static int roda_bench(const char *args, char *saida, size_t tam)
{
    char cmd[2048];

#ifdef _WIN32
    snprintf(cmd, sizeof cmd, "\"\"%s\" %s%s\"", bench, args, SEM_STDERR);
#else
    snprintf(cmd, sizeof cmd, "\"%s\" %s%s", bench, args, SEM_STDERR);
#endif
    FILE *p = popen(cmd, "r");
    if (!p) { saida[0] = '\0'; return -1; }

    size_t n = fread(saida, 1, tam - 1, p);   /* lê tudo o que o bench imprimiu */
    saida[n] = '\0';

    int st = pclose(p);
#ifdef _WIN32
    return st;                                /* Windows: já é o código de saída */
#else
    return (st == -1) ? -1 : WEXITSTATUS(st); /* POSIX: extrai o código do "status" */
#endif
}

/* Quebra uma linha CSV em campos (altera a string). Devolve quantos achou. */
static int separa_csv(char *linha, char *campos[], int max)
{
    int n = 0;
    char *c = linha;
    while (n < max) {
        campos[n++] = c;
        c = strchr(c, ',');
        if (!c) break;
        *c++ = '\0';
    }
    return n;
}

/* Conta linhas não vazias de um texto. */
static int conta_linhas(const char *s)
{
    int n = 0, tem = 0;
    for (; *s; s++) {
        if (*s == '\n') { if (tem) n++; tem = 0; }
        else if (*s != '\r') tem = 1;
    }
    return n + tem;
}

/* Tira \r e \n do final. */
static void apara(char *s)
{
    size_t n = strlen(s);
    while (n && (s[n - 1] == '\n' || s[n - 1] == '\r')) s[--n] = '\0';
}

/* ------------------------------------------------------------------------ */
static void teste_configuracoes(void)
{
    printf("[1] 12 configuracoes (4000 itens cada)\n");
    const char *versoes[] = { "A", "B" };
    int pcs[] = { 1, 4 };
    int bufs[] = { 1, 16, 256 };

    for (int v = 0; v < 2; v++)
        for (int i = 0; i < 2; i++)
            for (int j = 0; j < 3; j++) {
                char args[256], out[4096], esperado[128];
                snprintf(args, sizeof args, "teste %s %d %d %d 4000 7",
                         versoes[v], pcs[i], pcs[i], bufs[j]);
                int rc = roda_bench(args, out, sizeof out);
                int linhas = conta_linhas(out);
                apara(out);

                char *f[16];
                int nf = separa_csv(out, f, 16);
                int ok = (rc == 0 && linhas == 1 && nf == 10);

                if (ok) {
                    /* campos 0..6 devem repetir os argumentos */
                    snprintf(esperado, sizeof esperado, "teste,%s,%d,%d,%d,4000,7",
                             versoes[v], pcs[i], pcs[i], bufs[j]);
                    char obtido[128];
                    snprintf(obtido, sizeof obtido, "%s,%s,%s,%s,%s,%s,%s",
                             f[0], f[1], f[2], f[3], f[4], f[5], f[6]);
                    double tempo = atof(f[7]), vazao = atof(f[8]);
                    double calc = (tempo > 0) ? 4000.0 / tempo : 0;
                    ok = strcmp(obtido, esperado) == 0
                      && strcmp(f[9], "1") == 0                 /* checksum_ok */
                      && tempo > 0 && vazao > 0
                      && vazao > calc * 0.99 && vazao < calc * 1.01;
                }
                CHECK(ok, "versao %s  %dx%d  buffer %3d  (codigo %d, %d linha(s), %d campos)",
                      versoes[v], pcs[i], pcs[i], bufs[j], rc, linhas, nf);
            }
}

static void teste_argumentos(void)
{
    char out[4096];
    printf("[2] --help\n");
    CHECK(roda_bench("--help", out, sizeof out) == 0, "--help retorna 0");

    printf("[3] argumentos invalidos devem retornar 1\n");
    struct { const char *args, *desc; } casos[] = {
        { "",                              "sem argumentos" },
        { "teste C 1 1 16 4000 1",         "versao inexistente (C)" },
        { "teste A 1 1 16abc 4000 1",      "numero com lixo (16abc)" },
        { "teste A 1 1 -16 4000 1",        "numero negativo" },
        { "teste A 0 1 16 4000 1",         "zero produtores" },
        { "teste A 3 1 16 4000 1",         "total nao divisivel por P (4000/3)" },
        { "teste A 1 1 16 4000",           "faltando um argumento" },
    };
    for (size_t i = 0; i < sizeof casos / sizeof casos[0]; i++) {
        int rc = roda_bench(casos[i].args, out, sizeof out);
        CHECK(rc == 1, "%-36s -> codigo %d", casos[i].desc, rc);
    }
}

static void teste_latencia(void)
{
    char out[4096], linha[256];
    printf("[4] latencia em 1x1 buffer 1 (10000 itens -> 100 amostras)\n");
    remove(ARQ_LAT);
    int rc = roda_bench("teste B 1 1 1 10000 3 " ARQ_LAT, out, sizeof out);
    CHECK(rc == 0, "execucao com arquivo de latencia retorna 0");

    FILE *f = fopen(ARQ_LAT, "r");
    int n = 0, validas = 0;
    if (f) {
        while (fgets(linha, sizeof linha, f)) {
            apara(linha);
            if (!linha[0]) continue;
            n++;
            char *c[8];
            /* esperado: so,versao,rep,amostra,lat_ns */
            if (separa_csv(linha, c, 8) == 5 && strcmp(c[0], "teste") == 0 &&
                strcmp(c[1], "B") == 0 && strcmp(c[2], "3") == 0 && atof(c[4]) > 0)
                validas++;
        }
        fclose(f);
    }
    CHECK(f != NULL, "arquivo de latencia foi criado");
    CHECK(n == 100, "100 amostras gravadas (obtido %d)", n);
    CHECK(validas == n && n > 0, "todas as linhas com 5 campos e latencia > 0 (%d de %d)", validas, n);

    printf("[5] fora de 1x1 buffer 1 a latencia NAO e gravada\n");
    remove(ARQ_LAT);
    rc = roda_bench("teste A 4 4 16 4000 1 " ARQ_LAT, out, sizeof out);
    f = fopen(ARQ_LAT, "r");
    CHECK(rc == 0 && f == NULL, "4x4 buffer 16: codigo 0 e nenhum arquivo criado");
    if (f) fclose(f);
    remove(ARQ_LAT);
}

/* ------------------------------------------------------------------------ */
int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);   /* mostra cada linha na hora */

    if (argc >= 2) {
        snprintf(bench, sizeof bench, "%s", argv[1]);
    } else {
        /* Sem argumento (botão ▶ do CLion): usa a pasta deste executável. */
        snprintf(bench, sizeof bench, "%s", argv[0]);
        char *barra = strrchr(bench, '/');
        char *contra = strrchr(bench, '\\');
        if (contra > barra) barra = contra;
        if (barra) barra[1] = '\0'; else bench[0] = '\0';
#ifdef _WIN32
        strcat(bench, "bench.exe");
#else
        strcat(bench, "bench");
#endif
    }

    FILE *existe = fopen(bench, "rb");
    if (!existe) {
        printf("FALHA: executavel do bench nao encontrado em: %s\n"
               "Compile o alvo 'bench' antes ou passe o caminho como argumento.\n", bench);
        return 1;
    }
    fclose(existe);
    printf("bench: %s\n\n", bench);

    teste_configuracoes();
    teste_argumentos();
    teste_latencia();

    if (falhas) { printf("\nFALHA: %d verificacao(oes) com erro\n", falhas); return 1; }
    printf("\nOK\n");
    return 0;
}
