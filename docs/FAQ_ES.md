# MoonX360 — Preguntas frecuentes (FAQ)

[English](FAQ.md) · [Volver al README](../README.es.md)

---

### ¿El PC tiene que quedarse encendido?
**Sí.** MoonX360 es un cliente de streaming. Tu PC ejecuta el juego, codifica el video H.264 y el audio Opus, y los envía a tu Xbox 360 por la red local.

### ¿MoonX360 ejecuta juegos de PC de forma nativa en Xbox 360?
**No.** La Xbox 360 no puede ejecutar juegos modernos de PC x86/x64 de forma nativa. MoonX360 recibe video y audio desde tu PC y envía de vuelta los controles del mando.

### ¿Puedo jugar títulos de Steam, Epic Games o Xbox PC?
**Sí.** Cualquier juego o aplicación de escritorio que tu servidor compatible (Sunshine, Apollo u otro servidor GameStream) pueda iniciar o capturar funciona en MoonX360: Steam, Epic Games Store, Xbox PC Game Pass, GOG, emuladores y el escritorio de Windows.

### ¿Funciona con un teléfono móvil en lugar de un PC?
**No** en el uso habitual. MoonX360 requiere un servidor compatible con Moonlight/GameStream (como Sunshine o Apollo) ejecutándose en un PC.

### ¿Necesita Xbox Live?
**No.** MoonX360 funciona completamente en tu red local sin necesitar Xbox Live.

### ¿Funciona por Wi-Fi?
**Sí, con limitaciones.** Se recomienda Ethernet por cable (`100 Mbps`) para `720p60`. El Wi-Fi de Xbox 360 sufre mayor jitter y pérdida de paquetes; si usas Wi-Fi, empieza con el perfil **`Wireless Stable`** (`854×480 @ 60 FPS`, `4 Mbps`, paquetes de `768 B`).

### ¿Puedo usar un televisor CRT?
**Sí.** MoonX360 incluye el perfil **`CRT`** (`640×480 @ 60 FPS`, `3 Mbps`), relación `4:3`, filtros de escalado, ajuste de pantalla e interfaz `640×480`. El modo experimental **`CRT Retro (240p)`** (`640×240`) requiere salida `480i/60 Hz` y un plugin externo compatible `t240.xex` en DashLaunch. Consulta [CRT.md](CRT.md).

### ¿La Xbox 360 puede mostrar transmisiones a 1080p60?
**No.** La CPU de Xbox 360 decodifica H.264 por software; `1280×720 @ 60 FPS` (`6–10 Mbps`) es el objetivo principal a 60 FPS. `1920×1080` está disponible como **`1080p30 (Experimental)`** (`30 FPS`, `10 Mbps`) para escritorio o contenido con poco movimiento cuando el dashboard de Xbox 360 está en `1080p`.

### ¿Puedo transmitir por internet?
MoonX360 está diseñado y validado para red local (**LAN**). El uso por internet no está soportado y añade latencia y riesgos de exposición de red.

### ¿Por qué la transmisión da tirones?
- Activa el panel de rendimiento **Detailed** (`Back + LB + RB + X`).
- Si **`Dropped = 0`** y la decodificación es menor a `16.6 ms`, el host en Windows puede estar limitando una ventana que no está en pantalla completa.
- Si la decodificación supera `16.6 ms`, cambia de `High Quality (Experimental)` (`12 Mbps`) a **`Smooth`** (`6 Mbps`), **`Performance`** (`540p60`) o **`Standard`** (`720p30`).
- En redes cableadas con pérdida de paquetes, prueba fijar el tamaño de paquete en **`1024 bytes`** en **Stream → Video → Stream compatibility**.

### ¿Por qué no funciona el mando?
- Comprueba que **Desktop Mode** (`Back + R3`) esté **desactivado**.
- En Windows, asegúrate de tener instalado **ViGEmBus** (controlador virtual de mando Xbox 360 para Sunshine/Apollo).

### ¿Por qué falla la vinculación (pairing)?
- Confirma que PC y Xbox 360 están en la misma subred local y permite **TCP `47984`, `47989`, `48010`** y **UDP `47998–48010`** en el firewall.
- Activa **H.264** en el servidor del PC.
- Si cambió la IP del PC, usa **Computers → Host → Connection test**.

Consulta [TROUBLESHOOTING.md](TROUBLESHOOTING.md) para ver cómo enviar un reporte seguro (sin subir `client.pem`, `key.pem` ni `hosts.tsv`).
