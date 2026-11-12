# MoonX360: auditoria do decoder System GPU

Data: 12 de novembro de 2026 (America/Sao_Paulo).
Branch: `experiment/system-gpu-h264-20261112`.

**Resultado: integração nativa bloqueada por contrato privado incompleto.**
Esta branch contém o aplicativo MoonX360 completo, seleção persistente de decoder,
dispatch para o FFmpeg existente e duas correções visuais. Não contém um decoder
System GPU funcional. Nenhuma chamada privada H.264 foi implementada. Não há
resultado de execução ou comparação dos decoders em Xbox físico.

## 1. Release externa: o que foi inspecionado

Fontes primárias:

- [Release v0.1](https://github.com/HG-ERIK/Xbox360-GPU-Video-Decoding/releases/tag/v0.1).
- [README da tag 544c86c](https://github.com/HG-ERIK/Xbox360-GPU-Video-Decoding/blob/544c86c/README.md).
- [README atualizado, commit f03d00ee](https://github.com/HG-ERIK/Xbox360-GPU-Video-Decoding/blob/f03d00ee75450061a026136fe8cb3ba7e6fa782c/README.md).

O repositório na tag e no HEAD consultado tem apenas `README.md`. O arquivo
automático de código-fonte da tag foi baixado e aberto: contém somente o README.
O único asset de distribuição é `Xbox360-GPU-Video-Decoding-Demo-0.1.zip`:

- tamanho: 193729466 bytes;
- SHA-256: `7cb13d4ccd930ced76be3ab8ce8415a8bf39ceefdedd1780b5021f8e1421c82c`;
- tamanho e digest conferidos com a API da release; CRC de todos os membros verificado.

| Arquivo no ZIP | Bytes |
| --- | ---: |
| GPUVideoDemo/default.xex | 9580544 |
| GPUVideoDemo/Media/Fonts/Text_16.fnt | 1068636 |
| GPUVideoDemo/Media/Fonts/Text_22.fnt | 1068636 |
| GPUVideoDemo/Videos/demo_1080p30_30Mbps.mp4 | 107671148 |
| GPUVideoDemo/Videos/demo_720p60_15Mbps.mp4 | 76375606 |

Não há `.c`, `.cpp`, `.h`, `.lib`, PDB, mapa de símbolos nem documentação de ABI
nesse ZIP. O XEX da demonstração não foi executado, copiado para o MoonX360 ou
redistribuído. Os números de desempenho do README pertencem à demonstração do
autor e não são resultados desta integração.

## 2. APIs e procedência da confirmação

XDK local: `C:\Program Files (x86)\Microsoft Xbox 360 SDK`, versão 21256;
documentação CHM identificada como 21256.3. Também foram consultados headers e
bibliotecas da instalação RXDK. Hashes e detalhes do levantamento ficam em
`artifacts/system-gpu-audit-20261112/`, fora do pacote distribuível.

| Interface | Evidência encontrada | Estado para integração |
| --- | --- | --- |
| XAVInitialize | Declaração em `include/xbox/xav.h`, documentação `xbox360sdk.chm::/XAVInitialize.htm`, biblioteca `xav.lib` | Assinatura pública confirmada; não chamada nesta branch |
| XAVInit, XAVShutdown, XAVPreRender, XAVPostRender | Header e páginas correspondentes do CHM | Contrato público de inicialização/apresentação confirmado; não utilizado |
| H264InitParamStructureEx | Símbolo nos shims de `xav.lib` e `xavd.lib` | Protótipo privado completo e callbacks não estabelecidos |
| H264DecodeCreateEx | Mesma evidência de símbolo/dispatch | ABI completa não estabelecida |
| H264DecodeStart | Mesma evidência de símbolo/dispatch | ABI completa não estabelecida |
| H264DecodeGetOutputInfo | Mesma evidência de símbolo/dispatch | ABI, aquisição e sincronização não estabelecidas |
| H264DecodeGetOutputGPU | Mesma evidência de símbolo/dispatch | ABI e lease das superfícies não estabelecidos |
| H264DecodeReturnOutputGPU | Mesma evidência de símbolo/dispatch | ABI e devolução sob motor lock não estabelecidas |
| D3DDevice_ResetLowPriorityQueues | Procedimento e tipos CodeView do membro `lowpri.obj` em `d3d9.lib` | Tipos dos quatro argumentos e retorno confirmados por debug; sem declaração pública encontrada; sem prova de drain seguro |

`XAVInitialize` recebe `const XAVInit*` e um array de `DWORD` indexado por
`XAVThreadType`, retorna `HRESULT` e é declarado com `WINAPI`. O header define o
dispositivo, os parâmetros de apresentação e a política de cor em `XAVInit`.
Não foram recriados layouts próprios para substituir o header.

O procedimento de baixa prioridade tem retorno `HRESULT`, convenção `__cdecl`,
e argumentos `D3DDevice*`, `unsigned long`, `unsigned long`, `int`, nessa ordem.
A procedência é o registro de procedimento e sua lista de tipos/parâmetros em
CodeView, e não a dedução a partir dos literais do README.

O levantamento pode ser reproduzido com `python -B tools/audit_system_decoder_sdk.py`.
O script lê os arquivos locais, confere a associação dos tipos pré-compilados e
os registros de argumentos, e grava somente metadados JSON, sem extrair layouts
privados ou binários. Ele distingue símbolos definidos de meras referências.

Os símbolos H264 estão no membro de shim `extracodecsshim.obj`. Esse shim
despacha indiretamente por uma tabela; seu tipo genérico de debug `void()` não
é uma assinatura utilizável. Há tipos privados parciais em outros objetos de
`xav.lib`, mas isso não confirma o contrato completo de criação/leitura/notificação,
de aquisição/devolução de imagens, nem o lock do motor.

**Nenhum endereço, ordinal, offset, typedef ou estrutura privada foi inventado.**
Não foram copiados headers, bibliotecas, código-fonte, módulos do dashboard ou
dados de debug da Microsoft para o código ou pacote do MoonX360.

## 3. Acesso ao módulo do sistema

O autor descreve `XAVInitialize` carregando `dash.ExtraAVCodecs.xex` e fornecendo
o acesso ao dispositivo D3D da aplicação. Isso torna plausível a integração pelo
módulo instalado no console, sem incluir esse módulo no pacote. As bibliotecas
locais contêm os shims H264, mas a presença de símbolos não prova disponibilidade
ou compatibilidade em um console/dashboard específico.

A biblioteca local também referencia as famílias `XamLoadExtraAVCodecs` e
`XamUnloadExtraAVCodecs`. Isso sustenta a existência de um caminho de carga pelo
sistema, mas não substitui o contrato privado nem uma verificação em runtime.

O módulo não foi carregado ou sondado neste trabalho. Seu carregamento, versão,
comportamento em RGH/JTAG/BadAvatar e compatibilidade permanecem sem confirmação
física. Não se afirma que apenas encontrar/carregar o módulo tornaria o backend
apto. Nenhuma cópia de `dash.ExtraAVCodecs.xex` é necessária para a entrega atual.

## 4. Arquitetura real e limites

O fluxo existente é `app.c` → `video_options` → `video_init_callbacks()` →
callbacks MCC → `video_xdk.cpp` → FFmpeg → produtor de slots → render thread →
texturas/shaders e apresentação. `renderer` seleciona apresentação; não seleciona
o decoder H.264. O upload para texturas GPU não significa decode H.264 pela GPU.

Esta branch adiciona:

- `src/video_decoder.h`: preferência, nomes persistentes e resolução explícita
  da disponibilidade nesta compilação;
- `xdk/src/video_decoder_backend.h`: contrato interno do adaptador de callbacks;
- registro em `video_xdk.cpp` com apenas o backend FFmpeg realmente implementado;
- inicialização dos callbacks FFmpeg por esse registro, preservando setup/start/
  stop/cleanup/submit, slices e RFI existentes;
- rejeição no início de `stream()`, antes de `prepare_selected_app()` poder
  trocar/encerrar/iniciar um aplicativo no host;
- segunda rejeição no setup MCC para chamadores que contornem a UI; nenhum
  callback fictício de decode/start/stop/cleanup é instalado;
- identidade solicitada/ativa nos logs e em `decoder-session.txt`.

`video_set_options()` continua recusando alteração enquanto a sessão está
rodando. A preferência é copiada antes de anunciar capabilities e conectar.
Não há fallback durante uma sessão, tentativas concorrentes nem backend parcial
para destruir. Não foram introduzidos recursos ou filas GPU de decoder.

`video_cached.cpp` preserva seus três slots de apresentação e o histórico opcional
de smoothing; eles não são superfícies privadas do decoder do sistema. O renderer
existente trabalha com Y/U/V e a matriz limitada BT.601/709, e usa fences para
proteger as texturas. Crop/pitch/stride/layout de uma saída System GPU terão de
ser validados antes de reutilizar seus shaders. Não foi realizado readback
GPU→CPU, upload adicional ou conversão de uma superfície privada em `AVFrame`.

Não houve mudança em `Present`/`Swap`, nas filas de baixa prioridade, na política
de VSync, no Guide, no áudio, nos controles, no Quick Menu/HUD ou nos shaders CRT.
A preservação do comportamento foi verificada por build e testes de host; a
ausência de regressões físicas permanece pendente.

## 5. Arquivo local versus streaming MCC

| Demonstração de arquivo | MoonX360 em tempo real |
| --- | --- |
| Demux de MP4/MKV/MOV e possível conversão AVCC | MCC já entrega fragmentos de access units H.264 Annex B |
| Leitura antecipada e seek do arquivo | Fonte ao vivo, cancelamento, perda/FEC e reconexão |
| Autor limita entrada a aproximadamente um segundo à frente do áudio | Não importar esse orçamento: streaming requer limite bem menor e medição de idade |
| Clock de reprodução segue áudio e VBlank | Timestamps MCC incluem `presentationTimeUs`, `receiveTimeUs`, `enqueueTimeUs`, RTP 90 kHz e frameNumber |
| Configuração inicial e seek controlados pelo player | IDRs periódicos/recuperação podem repetir SPS/PPS; isso precisa de tratamento explícito |

`PDECODE_UNIT` contém `fullLength` e uma cadeia `PLENTRY`, não planos de imagem.
O backend FFmpeg concatena e decodifica durante `submitDecodeUnit()`. MCC completa
o frame e libera seus buffers depois do retorno; um backend assíncrono não pode
guardar esses ponteiros. Ele precisaria possuir uma cópia comprimida limitada
ou adotar explicitamente o contrato pull/completion do MCC.

MCC chama `stop()` antes de terminar de juntar sua thread de decoder e chama
`cleanup()` depois. A versão GPU deverá respeitar essa fronteira para impedir
que callbacks acessem memória liberada. Não se pode reaproveitar cegamente
as rotinas de teardown do FFmpeg para um motor com trabalho GPU pendente.

Não há parser/preparador GPU de SPS/PPS/IDR ou mapeamento de PTS implementado.
O caminho FFmpeg existente permanece responsável pelas unidades recebidas.

## 6. Bloqueios de sincronização e implementação nativa

Antes de registrar um backend System GPU real são necessários:

1. Protótipos completos e verificáveis das APIs H264 e estruturas de init/sample/
   output, callbacks, convenções de chamada, retornos e propriedade dos buffers.
2. Acesso identificado ao motor lock e prova de sua ordem em relação a gfx/D3D
   ownership e locks de leitura/retorno. Um mutex próprio da aplicação não protege
   o motor interno.
3. Método comprovado de observar jobs pendentes e continuar yield points até
   o trabalho terminar, incluindo cancelamento, Guide, menu e saída do título.
   Um flag interno chamado Drain ou um fence de desenho da aplicação não basta.
4. Lease das imagens: GetOutput sob o lock correto, headers sobre os planos sem
   cópia, crop/stride/BT.601/709 validados, fence posterior ao último consumidor,
   e ReturnOutput somente quando esse fence concluir.
5. Política de SPS/PPS: repetição idêntica em IDR não deve reconfigurar o decoder;
   mudança real exige transição segura e nenhuma textura ainda em uso liberada.
6. Fila comprimida limitada, backpressure com cancelamento e identidade por PTS/
   geração. B-frames não permitem inferir ordem de apresentação pela submissão.
7. IDR/perda de pacotes sem anunciar RFI não comprovado. Erro irrecuperável deve
   parar e drenar totalmente antes de outra sessão.

O README relata crashes sem motor lock, reconfiguração perigosa por SPS/PPS
repetidos e necessidade de yield points para fechar. Esses relatos justificam
o bloqueio; não fornecem por si só os contratos que faltam.

O XDK documenta `XAVPreRender` logo após `Swap` e `XAVPostRender` antes de `Swap`
para seu player. Isso não autoriza substituir todo `Present` do MoonX360. O
Guide existente depende de apresentações durante streaming, menus e teardown.
Nenhum timeout foi criado para devolver uma imagem ainda usada pela GPU.

## 7. Configuração implementada

`Settings > Advanced > Decoder > Video decoder` usa o menu nativo existente.
As opções ficam explícitas no texto de ajuda:

| Opção | Comportamento nesta compilação |
| --- | --- |
| FFmpeg (Stable) | Inicial, inclusive em INIs antigos; executa o backend existente |
| System GPU (Experimental) | Salva a preferência; informa indisponibilidade e recusa a sessão antes de agir no host |
| Auto | Usa FFmpeg e registra o motivo; não declara GPU inicializada |

A chave global é `decoder_backend=ffmpeg`, `system_gpu` ou `auto`. Valores
desconhecidos voltam ao padrão seguro. Profiles e presets não sobrescrevem essa
preferência, permitindo comparar o mesmo perfil com seleções diferentes sem
migrar o formato dos perfis anteriores.

## 8. Galeria e Launch Coverage

O checkpoint inicial preservou exatamente as alterações locais herdadas de
Desktop, capas, galeria e Launch Coverage, incluindo ferramentas de coleta.
Esse checkpoint não é atribuído à implementação do decoder.

Foram corrigidos separadamente dois defeitos adicionais comprovados:

- `ui_draw_hide_image()` ocultava a capa selecionada, mas o rasterizador de
  snapshot restaurava a opacidade do fade. A ocultação agora atualiza o comando
  e seu alvo de fade, mantendo visível outra ocorrência da mesma imagem;
- a Library não invalidava o desenho após erro/cancelamento de refresh. Agora
  restaura a galeria antes da próxima leitura de controle, preservando seleção.

Regressões exercitam o caminho real `ui_launch_begin()` → `ui_draw_software()`
em 24 casos (4:3/720p, motion ligado/desligado, fases do fade e tick intermediário)
e quatro saídas de refresh: offline, sem pairing, erro HTTP e cancelamento.
Ambas as regressões falharam antes da correção e passaram depois.

## 9. Validação e entrega

Os resultados finais, contagens, advertências, commits, SHA-256 e caminho do XEX
estão no `RELATORIO.md` e `VALIDATION.json` do diretório separado de entrega.
Comandos relevantes executados:

```powershell
$env:MOONX_TEST_OUT = Join-Path (Get-Location) 'host-tests-system-gpu-final'
python xdk/tests/run_host_tests.py --keep-going

$env:XDK_BUILD_DIR = Join-Path (Get-Location) 'xdk\build\system-gpu-h264-20261112\candidate'
$env:XDK_CONFIGURATION = 'Release'
$env:XDK_OPTIMIZATION = 'aggressive'
python xdk/build.py app xex
```

Dependências foram copiadas de um build local com manifesto FFmpeg e hashes
verificados; o build valida novamente profile, patch de workers e bibliotecas.
O build gera o aplicativo completo, com 25 fontes; não é uma demo ou um XEX mínimo.
O XEX tem a SPA da aplicação conferida após embedding. XEX2/imports/Title ID são
inspecionados com `imagexex /dump`. Artefatos anteriores são protegidos por hashes.

Testes de host verificam contratos da aplicação usando APIs Xbox simuladas;
eles não afirmam desempenho nem funcionamento físico do sistema de vídeo.
Uma execução inicial da suíte coincidiu com a edição de headers e não terminou
limpa; ela não conta como validação final. O preview também precisou incluir o
novo setting no seu fixture; seus resultados finais são os considerados.

Não houve merge, push, publicação pública, mudança de pairing ou deploy no Xbox.

## 10. Comparação e roteiro físico pendente

**Nenhuma diferença de desempenho FFmpeg versus System GPU foi medida.**
System GPU não executa nesta compilação. Qualquer porcentagem de redução de CPU,
FPS ou latência seria inventada. CPU total/ocupação de threads e scanout não
foram medidos. A telemetria existente permanece disponível para o FFmpeg.

| Perfil | FFmpeg físico desta branch | System GPU físico |
| --- | --- | --- |
| 1280×720 / 60 FPS / 12 Mbps | Pendente | Bloqueado pela ABI |
| 1280×720 / 30 FPS | Pendente | Bloqueado pela ABI |
| 640×480 / 60 FPS / CRT | Pendente | Bloqueado pela ABI |
| 1920×1080 / 30 FPS | Pendente | Bloqueado pela ABI |
| 1920×1080 / 60 FPS | Pendente, experimental | Bloqueado; sem garantia |

Quando houver backend real, comparar o mesmo host/encoder/rede/perfil/bitrate,
com perfil H.264, level, slices, GOP, referências, B-frames e SPS/PPS registrados.
Não comparar o decode do arquivo do autor com streaming como se fossem o mesmo
experimento. Registrar ao menos três sessões por condição, execução identificada
pelo hash do XEX, início frio e reabertura; manter diagnóstico igual nos dois lados.

Registrar recebidos/decodificados/submetidos, idade e profundidade das filas,
descartes, decode/submissão/espera em p50/p95/p99, primeiro pacote/frame/apresentação,
tempo de parada/reinício, IDRs, perda/FEC, artefatos e referências corrompidas.
Os coletores existentes usam janelas limitadas; registrar janela e quantidade
de amostras. CPU wall time de submissão não é tempo de execução da GPU.

Validar Guide, HUD, Quick Menu, Launch Coverage, controles, áudio, VSync, 4:3 e
CRT durante início/stop/retry, fluxo sem frames, perda de rede e mudanças de SPS/
resolução. Verificar ausência de uso após ReturnOutput e de jobs ao destruir.
Frames efetivamente visíveis exigem observação/captura da saída; contadores de
`Present`, retorno de `Present`, `Swap` ou fences não são prova de scanout.
