# MoonX360: experimento QPEL/Chroma VMX128

Data: 12/11/2026 (America/Sao_Paulo).
Branch: `experiment/vmx128-qpel-chroma-ab-20261112`.
Baseline local: `4867832495304001d38c9f6e9857bcbf853a2f33`.

**Decisão atual: conservar todas as funções de streaming do MoonX360.**
As alternativas estão implementadas e compiladas apenas para o benchmark offline.
Não há medição física de ganho, perda ou cauda de latência. O console estava
desligado, conforme informado pelo usuário; a consulta inicial a TCP 730 expirou.
Nenhum deploy, reboot, title switch ou comando de escrita no console foi feito.

## Preservação

O working tree inicial estava limpo. A branch parte da versão local com artwork,
Launch Coverage, Quick Menu, Desktop/local keyboard e a abstração FFmpeg/System
GPU. A branch anterior, a principal e os 151 XEXs preexistentes foram preservados.
Hashes, estado inicial, fontes auditadas e consulta somente leitura estão em
`artifacts/vmx-ab-20261112/preservation.json` e `console-readonly.json`.

Não houve substituição do decoder, alteração do protocolo Moonlight, importação
de FFmpeg 0.7 completo, mudança de configuração normal do usuário ou ativação
de outra política de deblocking. Os outputs A e B usam diretórios separados.

## Dispatch atual auditado

`xdk/scripts/build_ffmpeg.py` adiciona os hooks Xbox ao final dos inicializadores
FFmpeg 1.2 `ff_h264qpel_init`, `ff_h264chroma_init`, `ff_h264dsp_init` e videodsp.
O FFmpeg local configura `ARCH_PPC=0`/`HAVE_ALTIVEC=0`: os hooks próprios são a
entrada VMX relevante. Os callbacks de pixels têm o mesmo formato básico de
argumentos do código externo, mas os contextos de inicialização são diferentes.

| Grupo | Condições para instalação no MoonX360 |
| --- | --- |
| QPEL 16×16, PUT/AVG, 16 posições | `tested`, profundidade 8, bit XVMX_QPEL e bit individual em `qpel_ok[0][op]` |
| QPEL 8×8, PUT/AVG, 16 posições | Mesmas condições, máscara `qpel_ok[1][op]` |
| Chroma 8 e 4, PUT/AVG | `tested`, profundidade 8, bit XVMX_CHROMA e `chroma_ok[size][op]` |
| QPEL/Chroma menores ou outras profundidades | Fallback do FFmpeg preservado |
| Deblocking | C durante streaming; nenhuma candidata nova foi instalada |

`video_xdk_init()` executa `ff_xbox_dsp_selftest()` e depois configura
`ff_xbox_vmx_flags = XVMX_ALL & ~XVMX_LOOPFILTER` (`0x0f`) e
`ff_xbox_loopfilter_mask = 0`. QPEL, Chroma, Prefetch e IDCT são solicitados;
os resultados individuais do autoteste continuam decidindo o que é instalado.
O autoteste salva `vmx-selftest.txt`; setup/`decoder-session.txt` registram os
flags e parâmetros efetivos. **Essas são configurações confirmadas no código;
as máscaras e o comportamento deste XEX no Xbox ainda não foram observados.**

O autoteste original permanece intacto. O experimento obtém os ponteiros C reais
com flags temporariamente zero, sem decoder aberto, e compara também os ponteiros
VMX brutos: uma máscara de autoteste não consegue esconder uma função ruim do
benchmark. Os flags são restaurados antes de qualquer sessão posterior.

## Fonte externa e compatibilidade

Multiplex360 foi fixado no commit
`bda33b8602b9a0b9869effdd5cd7c0fadcb34216`:

- [QPEL](https://github.com/HG-ERIK/multiplex360/blob/bda33b8602b9a0b9869effdd5cd7c0fadcb34216/external/ffmpeg360/libavcodec/xbox360/h264_qpel_xb.c).
- [Chroma](https://github.com/HG-ERIK/multiplex360/blob/bda33b8602b9a0b9869effdd5cd7c0fadcb34216/external/ffmpeg360/libavcodec/xbox360/h264_chroma_xb.c).
- [xb_thread.c](https://github.com/HG-ERIK/multiplex360/blob/bda33b8602b9a0b9869effdd5cd7c0fadcb34216/external/ffmpeg360/libavcodec/xb_thread.c).
- [ffglue.c](https://github.com/HG-ERIK/multiplex360/blob/bda33b8602b9a0b9869effdd5cd7c0fadcb34216/src/ffglue.c).

O código externo instala QPEL/Chroma em `DSPContext` do FFmpeg 0.7. O MoonX360
usa `H264QpelContext` e `H264ChromaContext` do FFmpeg 1.2. A adaptação exporta
somente tabelas próprias de ponteiros para funções de pixels. Não há cast entre
esses contextos, link com uma biblioteca 0.7 ou chamada a seus inicializadores.

O `ffglue.c` externo solicita frame threading e o `xb_thread.c` usa o mapa
`{3,4,5,2}`. O MoonX360 usa o seu slice threading/mapa instrumentado
`{1,2,4,3,5}` e normalmente quatro workers. Nenhum código de threading ou política
de referências, fast decode ou descarte de B-frames do Multiplex360 foi portado.

## Diferenças técnicas

| Aspecto | MoonX360 atual | Multiplex360 adaptado |
| --- | --- | --- |
| QPEL horizontal/vertical | Filtro de seis taps, shifts/adds em lanes de 16 bits | Mesmo filtro e pesos; organização em HPass/VPass |
| Centro HV | Identidade equivalente com operações de 16 bits e saturação nos pontos de clipping | Sign extension para 32 bits, segundo filtro, +512 e shift 10 |
| Arredondamento | +16/shift 5 nas meias posições; médias arredondadas | Mesmas regras, comparadas contra C 1.2 |
| Estado intermediário QPEL | Janelas de vetores, especialização por wrapper; sem arrays de imagens intermediárias | Dois arrays de 256 bytes; HV usa dois arrays de 21 vetores (672 bytes); V usa duas janelas de seis vetores |
| Registradores/stack | Compilador pode introduzir spills | Compilador pode introduzir spills; tamanhos acima são declarações, não pico de stack medido |
| Chroma | Bilinear separável de 16 bits; multiplicações pequenas por máscaras/shifts/adds | Mesma identidade, pequenas multiplicações com testes dos bits de x/y |
| Chroma x=y=0 | Caminho de cópia/média direta | Mantém o caminho geral do candidato |
| Chroma y=0, x≠0 | Código atual calcula a próxima linha na via geral | Evita a leitura da próxima linha quando não é necessária |
| Stores de blocos curtos | Stores exatos de 4/8 bytes; fallback exato para desalinhamento | Adaptado para o mesmo contrato de largura exata |
| Referências H.264 | Recebe ponteiros do decoder; não muda DPB ou filtros | Também só lê planos; não gerencia nem altera o DPB |

Adaptações deliberadas, além da separação dos contextos:

1. O original usa read/modify/write de 16 bytes para saídas de 4/8 pixels. Mesmo
   restaurando os valores vizinhos, isso escreve fora do bloco e pode disputar
   memória com outra thread. A adaptação usa `vmx_ab_store.h`, com stores exatos;
   PUT curto também deixa de ler o destino para mascarar bytes vizinhos.
2. `Combine` não faz aritmética sobre um ponteiro NULL para o segundo plano.
3. QPEL 8 não carrega desnecessariamente um segundo vetor em `src+14`.

Portanto os tempos futuros serão da variante **Adapted**, identificada por hash,
e não uma alegação sobre o binário original do Multiplex360.

O dumpbin do XDK foi executado sobre os objetos compilados; a evidência local
inclui `assembly-static-counts.csv` e dumps de disassembly. As contagens são de
sites estáticos por símbolo, incluindo helpers e eventuais spills: não são
loads/stores executados por bloco ou ciclos. O objeto Moon também contém IDCT e
deblocking; somar objetos inteiros não é uma comparação válida de QPEL/Chroma.

O modelo de host contou 33.939.456/39.559.168 bytes de loads vetoriais lógicos e
15.400.960/19.922.944 bytes de stores vetoriais lógicos para Moon/Adapted na matriz
sintética inteira. Isso inclui temporários, não inclui todo acesso escalar, não
modela cache/VMX128 nativo e **não demonstra ganho ou perda de desempenho**.

## Benchmark nativo implementado

O build `XDK_VMX_AB_EXPERIMENT=1` mantém o aplicativo completo. Apenas sua entrada
existente `Settings > Advanced > Technical diagnostics > Decoder benchmark`
executa a comparação offline das três variantes. Não existe nova opção normal
de decoder, perfil ou seleção por bloco. O build A, com flag zero, preserva o
benchmark/replay anterior e pode armar uma gravação real de streaming.

`vmx_ab.c` mede:

- QPEL 16×16 e 8×8, 16 posições, PUT/AVG: 64 rotinas;
- Chroma 8 e 4, x/y=0..7, PUT/AVG: 256 combinações; alturas 2/4/8 na integridade
  e altura 8 no timing;
- três variantes compiladas pelo mesmo Jul12/XDK e flags FFmpeg agressivos;
- strides 64/80/96/128; fontes em 16 alinhamentos na integridade e alinhadas/
  deslocadas em sete bytes no timing;
- 32 operações de aquecimento e 31 amostras de 64 operações por célula;
- ordem das variantes rotacionada, clock QPC/frequência registrados;
- tempos brutos e um piso de chamada noinline, com resultados líquidos separados.

Há 7.680 células de timing quando todas as funções passam. **p50/p95/p99 são
percentis de médias de lotes de 64 operações**, não percentis de uma única
operação nem de frames. Com 31 amostras, p99 é o máximo observado. Usar tempos
brutos como evidência principal e repetir execuções; o piso contém seu próprio
custo e não é uma estimativa exata de todo overhead. Não há clock GPU ou scanout.

O worker fica no HW0, com a UI principal mantendo desenho e cancelamento B.
Preempção/interferência da UI entra no tempo de CPU observado; o experimento não
afirma isolamento perfeito. Buffers do microbenchmark são fixos. Emissão de CSV,
preparação dos padrões, comparações e sort dos percentis ficam fora dos intervalos
cronometrados. Não há alocação, lock ou escrita em disco no interior de cada bloco.

Se houver um `capture.bin` MLC1 válido, o worker decodifica um frame com C, sem
fast decode, copia um tile Y de 128×48, fecha o decoder e usa o tile na matriz.
O CSV registra origem, geometria e FNV do capture. No timing, stride 128 usa esse
tile; demais strides usam dados pseudoaleatórios determinísticos. Sem capture,
`real_reference=unavailable`: somente dados sintéticos são testados. Chroma usa
o mesmo tile de pixels como dados de entrada, não uma alegação de plano U/V real.

Cada execução cria um `moonlight/vmx-ab-<ticks>-<sequência>.csv` separado. Não
sobrescreve resultados anteriores. Cancelamento e falha de I/O não são relatados
como benchmark completo. Nenhuma candidata é instalada mesmo após passar.

## Integridade e limites da evidência

Antes de cronometrar, o harness compara a área inteira dos três destinos e a
imutabilidade da fonte. Confere prefixos, sufixos, linhas vizinhas e padding,
repete as operações duas vezes e combina zeros, 255, checkerboard, linhas/colunas
alternadas, rampas e dados pseudoaleatórios. Um tile real é adicional quando
disponível. As duas posições de borda mantêm o padding exigido pelo filtro e
pelos loads vetoriais dentro do buffer alocado.

No Xbox, o destino segue o contrato de alinhamento declarado pelo FFmpeg:
QPEL alinhado à largura e Chroma a oito bytes, com strides múltiplos de 16.
As fontes continuam desalinhadas em todas as 16 posições. O host cobre também
os 16 offsets de destino como teste adicional de robustez; isso não amplia a
ABI suportada no streaming. Não foram criadas chamadas nativas C deliberadamente
fora de seu contrato para tentar provocar exceções no console.

Resultados atuais em Windows:

| Verificação | Resultado |
| --- | --- |
| Checks de tabelas (320 IDs × três variantes) | 960/960 PASS |
| Casos de integridade | 106.496 |
| Verificações por variante/repetição, incluindo C/guardas | 638.976 |
| Comparações independentes de candidatas contra C | 425.984, zero divergências |
| Store fora de bloco mantendo bytes idênticos | Controle negativo detectado pelo modelo de acessos |
| Tempos produzidos em host | Zero; emulação não é medição Xenon |
| Referências reais testadas nesta execução host | Indisponíveis |
| Regressões pertinentes existentes | 12/12 suites PASS |
| Cache de build | 3/3 PASS |
| Rejeição de modo de biblioteca/hash incorreto | 2/2 testes PASS |
| Coletor | Recusa log de host como timing nativo; template de 320 rotinas com ganhos null |

`make_vmx_mc_test.py` compila os corpos originais dos templates C do FFmpeg 1.2
local. O host altera somente configuração de endianness/includes de suporte.
Os kernels atuais são extraídos do arquivo real e os adaptados vêm dos arquivos
compiláveis; intrinsics são modelados em bytes/lanes big endian. Stores parciais
e por palavra são modelados, com escopo de escrita por bloco e uma margem
conservadora para loads. O controle negativo mostra por que comparar valores
de canários sozinha não detecta stores redundantes fora do bloco.

Isso não prova instruções VMX128, alinhamento/faults reais, especulação, caches,
multithreading ou desempenho. Não foram feitos testes com páginas protegidas no
Xbox. A execução nativa da matriz continua obrigatória antes de qualquer ativação.

## Decisão por rotina

A entrega inclui `routine-results.csv`, com uma linha para cada uma das 320
rotinas/combinações. Todas têm ganhos, p50 e p99 vazios/null e decisão
`KEEP_CURRENT_PENDING_HARDWARE`. O teste host permite manter as candidatas no
experimento offline, mas não demonstra superioridade. Nenhuma foi integrada ao
streaming, nem rejeitada por um ganho/perda de desempenho inventado.

```powershell
# Agora: tabela sem medições
python tools/summarize_vmx_ab.py --output artifacts/vmx-ab-20261112/results-pending

# Depois de coletar CSVs nativos completos e comparáveis
python tools/summarize_vmx_ab.py run1.csv run2.csv run3.csv --output artifacts/vmx-native-results
```

O coletor exige origem nativa, checks completos e término com resultado zero.
Não promove uma média agregada que esconda piora de p99 em uma célula. Mesmo
vitórias repetidas no microbenchmark ficam pendentes de ruído e streaming.

## Build, arquivos e execução física posterior

```powershell
python xdk/tests/run_vmx_mc_tests.py
python xdk/tests/test_vmx_ab_build_contract.py

# Usar outputs separados com as dependências correspondentes já verificadas.
$env:XDK_BUILD_DIR = Join-Path (Get-Location) 'xdk/build/vmx-ab-20261112/experiment'
$env:XDK_OPTIMIZATION = 'aggressive'
$env:XDK_CONFIGURATION = 'Release'
$env:XDK_VMX_AB_EXPERIMENT = '1'
python xdk/scripts/build_ffmpeg.py avcodec
python xdk/build.py app xex
```

O baseline usa flag zero e outro diretório. O link verifica modo experimental,
hashes dos inputs VMX e das bibliotecas, além do manifesto/worker map existentes.
Release experimental compilou 467 fontes de avcodec; cada aplicativo compilou
25 fontes. Ambos têm os mesmos 137 avisos preexistentes da aplicação, sem novos
tipos/mensagens. Caminhos finais, hashes, checks XEX2/SPA/imports e commits estão
no `RELATORIO.md`/`VALIDATION.json` da entrega.

Arquivos principais: `multiplex_qpel.c`, `multiplex_chroma.c`, `vmx_ab_store.h`,
`vmx_ab.h`, `vmx_ab.c`, `h264_vmx.c`, `vmx_ab_xdk.h`, `video_xdk.cpp`, os dois
scripts de build, os testes de modelo/contrato, o coletor e este guia.

Quando o Xbox estiver ligado:

1. Usar diretórios A/B separados; manter o XEX estável. Identificar cada execução
   pelo hash, confirmar módulo carregado e recolher `vmx-selftest.txt`.
2. Se necessário, usar o benchmark do build A para armar `capture.bin`, transmitir
   e parar normalmente; a gravação existente é limitada a 600 frames/40 MiB.
3. Executar o benchmark do build B fora de uma sessão, repetir pelo menos três
   vezes e guardar CSVs completos, inclusive falhas/cancelamentos. Confirmar a
   presença do tile real e repetir condições de afinidade/temperatura/carga.
4. Revisar integridade nativa, padding e p50/p95/p99 por posição/stride. Só preparar
   um dispatch experimental para rotinas que mostrem ganho repetido sem piorar
   a cauda, preservando fallback e sem dispatch/lock/alocação por bloco adicional.
5. Então fazer A/B de streaming com o mesmo host/encoder/rede: 640×480/60 CRT,
   1280×720/30, 1280×720/60 a 12 Mbps e 1920×1080/30 experimental. Registrar GOP,
   slices, refs, B-frames, perfil/level, Fast Decode e RFI iguais.
6. Medir decode p50/p95/p99, RX/decoded/Present separadamente, descartes, stalls,
   artefatos, IDRs, Quick Menu input→submissão, e ciclos de desconexão/reconexão.
   Preservar os relatórios existentes de performance/latência/UX. Submissão CPU,
   execução GPU, retorno de Present e scanout têm evidências diferentes.

O build B entregue **também conserva o dispatch atual durante streaming**. Uma
comparação de streaming A/B com esses dois XEXs serve como controle de regressão,
nunca como evidência de ganho das funções externas, que continuam offline.
Se não houver ganho ou se a cauda piorar, conservar o código atual por rotina.
