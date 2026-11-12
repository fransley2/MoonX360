# MoonX360: resultados nativos QPEL/Chroma, 12/11/2026

**Decisão: conservar o dispatch de streaming atual do MoonX360.**
As candidatas Multiplex360 Adapted passaram a integridade nativa, mas nenhuma
das 320 combinações atendeu ao critério conservador de ganho repetido sem piora
de p99 em todas as células/runs. As candidatas continuam somente no benchmark.

## Evidência física e identidade

Console fornecido pelo usuário: `192.168.15.10`. FTP e XBDM responderam; Aurora
foi identificado no módulo de título antes do teste. O XEX estável foi baixado
para cópia de recuperação e seu hash foi conferido depois das três rodadas:

`18316a368c3d8a515951ffad61d1f4cd04ca03982b96701b30884db99e5b1fea`.

As rodadas R1/R2/R3 usaram o mesmo XEX experimental original:

`2adb5bf301e835096764a0995ff94cc979bcfd18c16b6833e4b699572b812482`.

Ele foi enviado a uma nova subpasta `VMX-Physical-20261112-175526-R1` sob a
instalação MoonX360, sem sobrescrever arquivos existentes. O readback do XEX via
FTP confirmou seus bytes antes do lançamento. O mecanismo existente
`moonlight/run-benchmark.once` foi consumido a cada inicialização. Módulo de
título, logs na pasta isolada e CSVs novos confirmam execução; o ACK de
`magicboot` não foi usado sozinho como prova de lançamento.

Dados brutos e hashes estão em
`artifacts/vmx-ab-20261112/physical-20261112-175526/`:

| Rodada | SHA-256 do CSV nativo |
| --- | --- |
| R1 | 92e718761806c936a0e14b468f38ef98728c03f2d5addcc05fd19050247a09a1 |
| R2 | f914ec9b66962b2c08d90a1be0a8e7be45b2b5874a2a84069a30c0f61c46605f |
| R3 | dab627110d96a23d5272b574a3e4a1b09eeffc0fb485a4fd48b62e3c42e87781 |

As três execuções têm 960 checks PASS, 7.680 células de timing e
`run_complete=1,result=0`. O autoteste de inicialização também confirmou
QPEL16/QPEL8 PUT/AVG `ffff`, Chroma8/Chroma4 `1/1`, IDCT `f`, sem divergências.
O deblocking C continuou selecionado apesar de os testes LF também passarem.

Após R3, o retorno ao Aurora foi confirmado por `modules` e `getexecstate`, e o
hash do XEX estável permaneceu igual. Não houve reboot frio, alteração de
launch.ini, mudança de pairing ou comandos enviados ao host GameStream.

## O que foi medido

- CPU QPC nativo, frequência registrada de 50 MHz.
- 32 operações de aquecimento por célula/variante.
- 31 amostras de 64 operações, em ordem de variantes rotacionada.
- QPEL 16×16/8×8, todas as posições PUT/AVG; Chroma 8/4, x/y=0..7, PUT/AVG.
- Quatro strides e dois alinhamentos de fonte no timing; integridade usa a
  matriz ampliada descrita no guia do experimento.
- Três repetições com o mesmo XEX e a mesma biblioteca/flags.

**Os percentis são de médias de lotes de 64 operações.** Não representam
latência individual de cada bloco ou de um frame. Com 31 amostras, p99 é o máximo
do conjunto. Tempos brutos são a evidência principal; o piso de chamada também
foi registrado. Não são timestamps GPU, frames apresentados ou scanout.

O worker e a UI compartilham HW0. Ruído de scheduling/UI/sistema e observações
XBDM estão incluídos nos tempos de CPU. Picos isolados não identificam uma causa
intrínseca do kernel. O critério conservador protege contra promover uma pequena
melhora de mediana que não se sustenta nas outras células ou na cauda.

## Exemplos medidos

Stride 64, fonte alinhada, destino conforme o contrato FFmpeg. p50 abaixo é a
mediana dos p50 das três execuções; p99 é o maior p99 dessas três células.
Valores em ns/op derivados dos lotes, arredondados nesta tabela.

| Rotina | p50 Moon | p50 Adapted | p99 Moon | p99 Adapted | Leitura |
| --- | ---: | ---: | ---: | ---: | --- |
| QPEL16 PUT (0,0) | 132,5 | 132,5 | 156,3 | 139,4 | Empate de mediana nesta célula; sem ganho geral |
| QPEL16 PUT HV (2,2) | 644,4 | 1491,9 | 696,3 | 1579,4 | Adapted usa 2,32× o tempo de p50 |
| QPEL16 AVG HV (2,2) | 664,4 | 1530,3 | 721,3 | 1596,3 | Adapted usa 2,30× o tempo de p50 |
| QPEL8 PUT HV (2,2) | 287,8 | 604,4 | 307,8 | 635,0 | Adapted usa 2,10× o tempo de p50 |
| Chroma8×8 PUT (0,0) | 86,3 | 225,9 | 112,5 | 299,7 | Caminho full-pel atual é mais rápido |
| Chroma8×8 AVG (0,1) | 281,3 | 270,9 | 298,1 | 343,1 | Pequena melhora de mediana, cauda pior |

A tabela completa, com 320 linhas, está em `native-three-runs/routine-results.csv`.
Seu p50 agregado toma a mediana das células, e p99 agregado toma o máximo; os
CSVs brutos permitem comparar cada stride/alinhamento. A decisão exige avaliar
as células correspondentes de cada run, sem deixar o agregado ocultar perdas.

O coletor classificou 320/320 linhas como `KEEP_CURRENT_TAIL_LOSS`: para cada
combinação houve ao menos uma célula/run com p99 Adapted acima de Moon. Isso não
significa que todas as amostras de todas as rotinas foram mais lentas, nem
estabelece significância estatística para diferenças minúsculas. Não houve uma
vitória robusta que justificasse habilitação. Rotinas HV e full-pel Chroma têm
também perdas de mediana substanciais nos exemplos acima.

## Cancelamento do diagnóstico

Já havia seis CSVs na pasta B instalada pelo usuário. Todos tinham somente
cabeçalhos e `result=2`, sem uma única rotina/timing. Foram preservados e não
contam como medição. Os arquivos não registram o estado dos botões/latches;
portanto não provam sozinhos a causa de cada cancelamento anterior.

A revisão encontrou um defeito reproduzível: `input_close_app_requested()` é um
comando remoto Close App que pode permanecer latched após uma sessão; não é um
pedido de saída do diagnóstico offline. A tela do benchmark o consultava e
cancelava imediatamente. Foi removido somente esse consumo fora de contexto,
mantendo o novo pressionamento de B como cancelamento normal.

`xdk/tests/test_vmx_ab_cancel.py` extrai a tela real: falhou antes da correção e
passou depois para latch remoto antigo e cancelamento novo por B. Thread/device
boundary nesse teste é simulado; isso não prova um ciclo Close App real no Xbox.

O build corrigido tem hash
`3dd7f5254c209184897febf5dc25d3eb444b145db9ae2533567d8380bb348117`.
Esse build também foi executado no Xbox: uma quarta rodada completa passou
os 960 checks e as 7.680 células, com resultado zero. Seu CSV tem SHA-256
`d88a87cc8bbc35e4f4f6031140a8c34c09fdc5730abcf94ffd6cbba9da2244a7`.
As evidências dessa quarta rodada ficam separadas das três medições
originais. A biblioteca avcodec e os kernels do microbenchmark são byte a byte
os mesmos; a alteração é no controle da tela. Ainda assim, resultados de hashes
de XEX diferentes não foram misturados para formar o grupo R1/R2/R3.

## Limites e decisão de integração

Os CSVs registram `real_reference=unavailable`. Não havia capture.bin na
instalação consultada; os testes físicos usaram padrões sintéticos. Não foram
medidos FPS de streaming, decode p50/p95/p99 de frames, CPU total, ocupação de
threads, descartes/stalls, Quick Menu input→photon, referências de jogo reais,
áudio/CRT/Guide em sessão ou latência de GPU/scanout.

Nenhuma candidata foi promovida ao streaming. Assim, não se apresenta uma
comparação A/B de streaming entre kernels que efetivamente não foram trocados.
Os perfis 640×480/60, 720p30, 720p60/12 Mbps e 1080p30 e testes de reconexão ficam
pendentes caso apareça uma candidata elegível para integração futura. O projeto
continua usando as rotinas atuais e o mesmo decoder/protocolo Moonlight.

A conclusão vale para estas implementações adaptadas, flags, harness, padrões e
console. Não é um veredito de desempenho do aplicativo Multiplex360 inteiro.
As diferenças de algoritmo, stores exatos e compatibilidade 0.7/1.2 estão em
`VMX128_AB_EXPERIMENT.md`. Não houve merge na principal ou publicação pública.
