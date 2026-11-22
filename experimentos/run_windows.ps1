# =============================================================================
# run_windows.ps1 — coleta COMPLETA dos dados no Windows (nativo, não WSL)
# =============================================================================
# Uso (PowerShell, na raiz do repositório, depois de compilar em Release):
#     powershell -ExecutionPolicy Bypass -File experimentos\run_windows.ps1
#
# Parâmetros opcionais:
#     -Bench  caminho do bench.exe  (padrão: procura em build\Release, build, cmake-build-release)
#     -So     rótulo no CSV          (padrão: windows)
#     -Total  itens por execução     (padrão: 400000)
#     -Reps   repetições             (padrão: 30)   ex.: -Reps 2 para teste rápido
#
# Faz exatamente o mesmo que run_linux.sh (mesmo desenho experimental):
# aquecimento, repetições intercaladas com ordem sorteada, parada em erro.
#
# ATENÇÃO À CODIFICAÇÃO: no Windows PowerShell 5.1 o operador ">>" grava em
# UTF-16, o que quebra o CSV. Por isso usamos Set-Content/Add-Content com
# -Encoding ascii.
# =============================================================================
param(
    [string]$Bench = "",
    [string]$So    = "windows",
    [int]$Total    = 400000,
    [int]$Reps     = 30
)
$ErrorActionPreference = "Stop"

# ---- Localiza o executável ---------------------------------------------------
if ($Bench -eq "") {
    $candidatos = @("build\Release\bench.exe", "build\bench.exe",
                    "cmake-build-release\bench.exe", "cmake-build-debug\bench.exe")
    foreach ($c in $candidatos) { if (Test-Path $c) { $Bench = $c; break } }
}
if ($Bench -eq "" -or -not (Test-Path $Bench)) {
    throw "bench.exe nao encontrado. Compile em Release antes (README) ou use -Bench <caminho>."
}
if ($Bench -like "*debug*") {
    Write-Warning "Usando build de DEBUG ($Bench). Para os dados oficiais compile em RELEASE."
}

$raw = "dados\${So}_raw.csv"
$lat = "dados\${So}_latencia.csv"
$amb = "dados\${So}_ambiente.txt"

# Protege os dados brutos: nunca sobrescreve uma coleta já feita.
if ((Test-Path $raw) -or (Test-Path $lat)) {
    throw "$raw ou $lat ja existe. Mova/apague manualmente se quiser coletar de novo."
}
New-Item -ItemType Directory -Force dados | Out-Null

# ---- Registro do ambiente ----------------------------------------------------
$os  = Get-CimInstance Win32_OperatingSystem
$cpu = Get-CimInstance Win32_Processor | Select-Object -First 1
$cs  = Get-CimInstance Win32_ComputerSystem
$plano = (powercfg /getactivescheme) -join " "
@(
    "data: $(Get-Date -Format o)"
    "sistema: $($os.Caption) versao $($os.Version) build $($os.BuildNumber)"
    "cpu: $($cpu.Name) | nucleos $($cpu.NumberOfCores) | logicos $($cpu.NumberOfLogicalProcessors)"
    "memoria_GB: $([math]::Round($cs.TotalPhysicalMemory / 1GB, 1))"
    "fabricante/modelo: $($cs.Manufacturer) / $($cs.Model)   (VM aparece aqui: VirtualBox, VMware...)"
    "plano_energia: $plano"
    "bench: $Bench"
    "parametros: Total=$Total Reps=$Reps"
) | Set-Content -Encoding ascii $amb

# ---- Cabeçalhos ---------------------------------------------------------------
"so,versao,prod,cons,buf,total,rep,tempo_s,vazao,checksum_ok,user_s,kernel_s" | Set-Content -Encoding ascii $raw
"so,versao,rep,amostra,lat_ns" | Set-Content -Encoding ascii $lat

# ---- As 12 configurações ------------------------------------------------------
$configs = @()
foreach ($v in "A", "B") {
    foreach ($pc in 1, 4) {
        foreach ($b in 1, 16, 256) {
            $configs += , @($v, $pc, $pc, $b)
        }
    }
}

# ---- Aquecimento (descartado) ------------------------------------------------
& $Bench $So A 1 1 16 $Total 0 | Out-Null

# ---- Coleta -------------------------------------------------------------------
$inicio = Get-Date
for ($rep = 1; $rep -le $Reps; $rep++) {
    # Get-Random -Count N sobre a lista inteira = embaralhar (funciona no PS 5.1)
    $ordem = $configs | Get-Random -Count $configs.Count
    foreach ($cfg in $ordem) {
        $linha = & $Bench $So $cfg[0] $cfg[1] $cfg[2] $cfg[3] $Total $rep $lat
        if ($LASTEXITCODE -ne 0) {
            throw "ERRO na execucao: $($cfg -join ' ') rep $rep (checksum ou falha)"
        }
        Add-Content -Encoding ascii -Path $raw -Value $linha
    }
    $seg = [int]((Get-Date) - $inicio).TotalSeconds
    Write-Host "repeticao $rep/$Reps concluida ($seg s)"
}

Write-Host "Pronto: $raw, $lat, $amb"
