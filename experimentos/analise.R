# Rode UMA vez no console do R (nao deixe dentro do script):
# install.packages(c("readr", "dplyr", "tidyr", "ggplot2", "scales"))

library(readr)
library(dplyr)
library(tidyr)
library(ggplot2)
library(scales)

# 1. Configuração
PASTA_DADOS    <- "dados"
PASTA_SAIDA    <- Sys.getenv("PASTA_SAIDA", "experimentos/resultados")
# Sistemas lidos (rotulo usado em -So / argumento <so> do bench). Para analisar uma
# coleta piloto sem misturar com a real: SISTEMAS=piloto PASTA_SAIDA=... Rscript ...
SISTEMAS       <- strsplit(Sys.getenv("SISTEMAS", "linux,windows"), ",")[[1]]
ARQ_VAZAO      <- file.path(PASTA_DADOS, paste0(SISTEMAS, "_raw.csv"))
ARQ_LATENCIA   <- file.path(PASTA_DADOS, paste0(SISTEMAS, "_latencia.csv"))
REPS_ESPERADAS <- 30 # por configuração
IGNORAR_REP_0  <- TRUE # os run_*.ps1/.sh descartam o aquecimento ANTES de gravar (nao vai ao CSV);
                       # a flag so protege caso alguem grave uma rep 0 manualmente

COR_VERSAO <- c(A = "#0072b2", B = "#D55E00")
ROT_VERSAO <- c(A = "A: variável de condição", B = "B: semáforo")
rot_so <- function(x) ifelse(x == "linux", "Linux",
                      ifelse(x == "windows", "Windows", x))

dir.create(PASTA_SAIDA, recursive = TRUE, showWarnings = FALSE)
alerta <- function(...) message("\n[ATENCAO] ", ...)

# 2. Leitura
TIPOS_VAZAO <- cols(
  so = col_character(), versao = col_character(),
  prod = col_integer(), cons = col_integer(), buf = col_integer(),
  total = col_double(), rep = col_integer(),
  tempo_s = col_double(), vazao = col_double(), checksum_ok = col_integer()
)
TIPOS_LAT <- cols(
  so = col_character(), versao = col_character(),
  rep = col_integer(), amostra = col_integer(), lat_ns = col_double()
)

codificacao <- function(arq) {
  b <- readBin(arq, "raw", n = 2)
  if (length(b) == 2 && b[1] == as.raw(0xFF) && b[2] == as.raw(0xFE)) "UTF-16LE" else "UTF-8"
}

ler_csvs <- function(arquivos, tipos) {
  achados <- arquivos[file.exists(arquivos)]
  for (a in setdiff(arquivos, achados)) message("[aviso] nao encontrado, ignorando: ", a)
  if (length(achados) == 0) return(NULL)
  
  esperadas <- names(tipos$cols)
  bind_rows(lapply(achados, function(arq) {
    d <- read_csv(arq, col_types = tipos, progress = FALSE,
                  locale = locale(encoding = codificacao(arq)))
    if (nrow(problems(d)) > 0) { print(problems(d)); stop(basename(arq), ": linhas com formato inesperado") }
    ausentes <- setdiff(esperadas, names(d))
    if (length(ausentes) > 0)
      stop(basename(arq), ": faltam as colunas ", paste(ausentes, collapse = ", "),
           ". Colunas encontradas: ", paste(names(d), collapse = ", "))
    d$arquivo <- basename(arq)
    d
  }))
}

vz <- ler_csvs(ARQ_VAZAO, TIPOS_VAZAO)
if (is.null(vz)) stop("Nenhum CSV de vazao encontrado. getwd() = ", getwd(),
                      ". Rode da raiz do repositorio e confira ARQ_VAZAO.")
cat("Arquivos de vazao lidos:", paste(unique(vz$arquivo), collapse = ", "), "\n")
cat("Linhas de vazao lidas:", nrow(vz), "\n")

# 3. Validação
if (IGNORAR_REP_0) {
  cat("Aquecimento (rep 0) ignorado na analise:", sum(vz$rep == 0), "linha(s)\n")
  vz <- filter(vz, rep != 0)
}

# 3a. repetição duplicada = reexecução anexada ao mesmo CSV
chave <- c("so", "versao", "prod", "cons", "buf", "rep")
dups <- vz %>% group_by(across(all_of(chave))) %>% filter(n() > 1) %>% ungroup()
if (nrow(dups) > 0) {
  alerta(nrow(dups), " linhas com (so, versao, prod, cons, buf, rep) repetido. ",
         "Provavel reexecucao anexada ao CSV. Salvo em duplicadas_vazao.csv")
  write_csv(dups, file.path(PASTA_SAIDA, "duplicadas_vazao.csv"))
}

# 3b. checksum falhou = bug de sincronização, NÃO é lentidão
invalidas <- filter(vz, checksum_ok != 1)
if (nrow(invalidas) > 0) {
  alerta(nrow(invalidas), " execucao(oes) com checksum FALHO foram EXCLUIDAS dos calculos. ",
         "Isso indica erro de sincronizacao: investigue antes de entregar. ",
         "Salvo em execucoes_checksum_falhou.csv")
  write_csv(invalidas, file.path(PASTA_SAIDA, "execucoes_checksum_falhou.csv"))
  vz <- filter(vz, checksum_ok == 1)
}

# 3c. o enunciado exige o MESMO total de itens em todas as configurações
if (n_distinct(vz$total) > 1)
  alerta("total de itens diferente entre execucoes: ",
         paste(sort(unique(vz$total)), collapse = ", "))

# rótulos de configuração, na ordem 1x1, 4x4
niveis <- vz %>% distinct(prod, cons) %>% arrange(prod, cons) %>%
  mutate(r = paste0("P = ", prod, ", C = ", cons)) %>% pull(r)
vz <- vz %>% mutate(config = factor(paste0("P = ", prod, ", C = ", cons), levels = niveis))


# 4. Vazão: média e desvio
resumo <- vz %>%
  group_by(so, versao, config, prod, cons, buf) %>%
  summarise(n = n(),
            vazao_media    = mean(vazao),
            vazao_dp       = sd(vazao),                  # desvio amostral (n-1)
            cv_pct         = 100 * vazao_dp / vazao_media,
            tempo_medio_s  = mean(tempo_s),
            tempo_dp_s     = sd(tempo_s),
            .groups = "drop") %>%
  arrange(so, versao, prod, cons, buf)

cat("\nRepeticoes validas por configuracao (esperado:", REPS_ESPERADAS, ")\n")
print(select(resumo, so, versao, config, buf, n), n = Inf)
poucas <- filter(resumo, n < REPS_ESPERADAS)
if (nrow(poucas) > 0)
  alerta(nrow(poucas), " configuracao(oes) com menos de ", REPS_ESPERADAS,
         " repeticoes (exige justificativa no relatorio).")

write_csv(resumo, file.path(PASTA_SAIDA, "resumo_vazao.csv"))

# razão B/A: quantas vezes a Versão B é mais rápida (>1) ou mais lenta (<1)
if (all(c("A", "B") %in% resumo$versao)) {
  razao <- resumo %>%
    select(so, config, buf, versao, vazao_media) %>%
    pivot_wider(names_from = versao, values_from = vazao_media) %>%
    rename(vazao_A = A, vazao_B = B) %>%
    mutate(razao_B_sobre_A = vazao_B / vazao_A)
  cat("\nRazao de vazao B/A:\n"); print(razao, n = Inf)
  write_csv(razao, file.path(PASTA_SAIDA, "razao_B_sobre_A.csv"))
}

# 5 Gráfico 1: vazão x tamanho do buffer
pd <- position_dodge(width = 0.2)    # afasta A e B para as barras não se sobreporem

for (s in unique(resumo$so)) {
  d <- filter(resumo, so == s)
  n_txt <- if (min(d$n) == max(d$n)) min(d$n) else paste0(min(d$n), " a ", max(d$n))
  
  g <- ggplot(d, aes(x = factor(buf), y = vazao_media,
                     colour = versao, shape = versao, group = versao)) +
    geom_line(position = pd, linewidth = 0.7) +
    geom_point(position = pd, size = 2.4) +
    geom_errorbar(aes(ymin = pmax(0, vazao_media - vazao_dp),
                      ymax = vazao_media + vazao_dp),
                  position = pd, width = 0.15, linewidth = 0.5) +
    facet_wrap(~config, scales = "free_y") +
    scale_colour_manual(values = COR_VERSAO, labels = ROT_VERSAO, name = NULL) +
    scale_shape_discrete(labels = ROT_VERSAO, name = NULL) +
    scale_y_continuous(labels = label_number(scale = 1e-6),
                       expand = expansion(mult = c(0, 0.08))) +
    expand_limits(y = 0) +
    labs(title = paste("Vazão x tamanho do buffer —", rot_so(s)),
         subtitle = paste0("média \u00b1 desvio padrão (n = ", n_txt, " repetições)"),
         x = "Tamanho do buffer (itens)", y = "Vazão (milhões de itens/s)") +
    theme_bw(base_size = 10) +
    theme(legend.position = "bottom")
  
  ggsave(file.path(PASTA_SAIDA, paste0("vazao_", s, ".png")), g,
         width = 7.5, height = 3.4, dpi = 300)
}

# vazão x repetição (detecta deriva/aquecimento)
for (s in unique(vz$so)) {
  g <- ggplot(filter(vz, so == s), aes(x = rep, y = vazao, colour = versao)) +
    geom_line(alpha = 0.5) + geom_point(size = 0.8) +
    facet_grid(buf ~ config, scales = "free_y",
               labeller = labeller(buf = function(x) paste("buffer", x))) +
    scale_colour_manual(values = COR_VERSAO, labels = ROT_VERSAO, name = NULL) +
    scale_y_continuous(labels = label_number(scale = 1e-6)) +
    labs(title = paste("Diagnóstico: vazão ao longo das repetições —", rot_so(s)),
         x = "Repetição", y = "Vazão (milhões de itens/s)") +
    theme_bw(base_size = 9) + theme(legend.position = "bottom")
  ggsave(file.path(PASTA_SAIDA, paste0("diagnostico_rep_", s, ".png")), g,
         width = 8, height = 6, dpi = 200)
}


# 6. Latência
lt <- ler_csvs(ARQ_LATENCIA, TIPOS_LAT)

if (is.null(lt)) {
  message("\n[aviso] nenhum CSV de latencia encontrado: pulando os graficos de latencia.")
} else {
  if (IGNORAR_REP_0) lt <- filter(lt, rep != 0)
  lt <- mutate(lt, lat_us = lat_ns / 1000, so_rot = rot_so(so))
  
  resumo_lat <- lt %>%
    group_by(so, versao) %>%
    summarise(n_reps = n_distinct(rep), n_amostras = n(),
              media_us = mean(lat_us), dp_us = sd(lat_us),
              mediana_us = median(lat_us),
              p95_us = quantile(lat_us, 0.95), p99_us = quantile(lat_us, 0.99),
              max_us = max(lat_us), .groups = "drop")
  cat("\nLatencia (microssegundos):\n"); print(resumo_lat, width = Inf)
  write_csv(resumo_lat, file.path(PASTA_SAIDA, "resumo_latencia.csv"))
  
  # Gráfico 2 (box plot). Escala log: latências têm cauda longa.
  g_box <- ggplot(lt, aes(x = versao, y = lat_us, fill = versao)) +
    geom_boxplot(outlier.size = 0.4, outlier.alpha = 0.15, linewidth = 0.4) +
    facet_wrap(~so_rot) +
    scale_fill_manual(values = COR_VERSAO, labels = ROT_VERSAO, name = NULL) +
    scale_y_log10(labels = label_number(drop0trailing = TRUE)) +
    labs(title = "Distribuição da latência (buffer 1, 1 produtor x 1 consumidor)",
         x = NULL, y = "Latência (µs, escala log)") +
    theme_bw(base_size = 10) +
    theme(legend.position = "bottom", axis.text.x = element_blank(),
          axis.ticks.x = element_blank())
  ggsave(file.path(PASTA_SAIDA, "latencia_boxplot.png"), g_box,
         width = 7, height = 3.6, dpi = 300)
  
  # Alternativa do enunciado (histograma): mostra se a distribuição tem 2 picos.
  g_hist <- ggplot(lt, aes(x = lat_us, fill = versao)) +
    geom_histogram(bins = 80, position = "identity", alpha = 0.55) +
    facet_wrap(~so_rot) +
    scale_fill_manual(values = COR_VERSAO, labels = ROT_VERSAO, name = NULL) +
    scale_x_log10(labels = label_number(drop0trailing = TRUE)) +
    labs(title = "Histograma da latência (buffer 1, 1 produtor x 1 consumidor)",
         x = "Latência (µs, escala log)", y = "Amostras") +
    theme_bw(base_size = 10) + theme(legend.position = "bottom")
  ggsave(file.path(PASTA_SAIDA, "latencia_histograma.png"), g_hist,
         width = 7, height = 3.6, dpi = 300)
}

# ------------------- 7. REPRODUTIBILIDADE (vai no README) -----------------
writeLines(capture.output(sessionInfo()), file.path(PASTA_SAIDA, "sessionInfo.txt"))
cat("\nPronto. Arquivos em", PASTA_SAIDA, "\n")