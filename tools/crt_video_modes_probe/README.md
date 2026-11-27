# MoonX360 — CRT Video Modes Probe (`tools/crt_video_modes_probe/`)

Ferramenta diagnóstica **independente** do MoonX360 principal para validar modos de vídeo D3D9 e saída analógica CRT no hardware físico do Xbox 360.

## O que ela faz

1. Consulta `XGetVideoMode`, `XGetVideoFlags` e presença do plugin externo `t240.xex` via beacon `S240` v3 em `.data` (sem tocar registradores MMIO diretamente).
2. Testa `IDirect3DDevice9::Reset()` em modo seguro para os candidatos:
   - `640×480` (Safe SD)
   - `640×240` (CRT 240p)
   - `320×240` (Native 240p)
   - `1280×240` (Super Resolution 2x)
   - `1920×240` (Super Resolution 3x)
   - `1280×720` (HD 720p)
3. Mede 90 intervalos de `Present()` com `D3DPRESENT_INTERVAL_ONE` (`p50`, `p95`, `p99`, `max` em µs) para cada modo aceito e restaura imediatamente o modo seguro inicial.
4. Grava o relatório estruturado em `game:\crt-modes-probe.txt`.
5. Abre o visualizador interativo com 8 padrões CRT:
   - `1×1 Checkerboard`, `Horizontal 1px Lines`, `Vertical 1px Lines`, `Pixel Art 16×16 Grid`, `Luma + RGB Gradients`, `Horizontal Motion Bar`, `Vertical Motion Bar`, `Static Overscan Frame`.

## Controles

- **D-Pad Left / Right**: alternar padrão de teste CRT.
- **A**: testar próximo modo D3D9 candidato (reverte para modo seguro automaticamente se `Reset` falhar).
- **Y**: alternar solicitação `S240` (`FORCE_240P` / `FORCE_480I`) somente quando em `480i` com `t240.xex` v2.0+ detectado.
- **B**: restaurar modo seguro, salvar `crt-modes-probe.txt` e retornar ao dashboard.

## Compilação

```powershell
python projeto\tools\crt_video_modes_probe\build_probe.py
```

Saída isolada: `projeto/tools/crt_video_modes_probe/build/default.xex`.
