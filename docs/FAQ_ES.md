# MoonX360 — Preguntas frecuentes

[English](FAQ.md)

Respuestas a las preguntas más comunes de GitHub, Reddit y YouTube.

[Última versión estable](https://github.com/fransley2/MoonX360/releases/latest) · [Guía de configuración (en inglés)](SETUP.md)

---

## Preguntas generales

### ¿Qué es MoonX360?

MoonX360 es un cliente de streaming basado en Moonlight para consolas Xbox 360 modificadas.

El PC ejecuta los juegos y envía el video y el audio a la Xbox 360. La consola envía al PC las pulsaciones del mando.

Necesitas una Xbox 360 modificada y un PC con Sunshine, Apollo u otro servidor compatible con GameStream y con la codificación H.264 activada.

### ¿Funciona como Xbox Cloud Gaming (xCloud)?

Xbox Cloud Gaming ejecuta los juegos en los servidores de Microsoft. MoonX360 usa tu propio PC como servidor.

Para usar MoonX360 basta con un PC capaz de ejecutar los juegos y cumplir los requisitos de streaming. No hace falta una suscripción a Xbox Cloud Gaming.

### ¿El PC tiene que quedarse encendido?

Sí. El PC ejecuta el juego, codifica el video y lo envía a tu Xbox 360. Si apagas el PC, se corta la transmisión.

### ¿MoonX360 hace que mi Xbox 360 sea más potente?

La potencia de la consola sigue siendo la misma. MoonX360 permite ver y controlar desde la Xbox 360 un juego que se está ejecutando en otro equipo.

### ¿Por qué no conectar el PC directamente al televisor?

Puedes conectar el PC al televisor. MoonX360 te sirve si la Xbox 360 ya está conectada a otro televisor o a un CRT. Así puedes jugar con la consola y su mando sin mover el PC ni tender un cable de video hasta esa pantalla.

---

## Juegos y compatibilidad

### ¿Puedo transmitir cualquier juego de PC o solo juegos de Steam?

Puedes transmitir los juegos y aplicaciones que tu servidor pueda iniciar o capturar.

- Steam
- Epic Games Store
- Xbox PC Game Pass
- GOG
- Otros lanzadores de PC
- Emuladores
- El escritorio de Windows

Algunos juegos requieren ajustes adicionales de captura o del mando.

### ¿Puedo jugar a GTA 6 u otros juegos modernos?

Si existe una versión compatible para PC, tu equipo puede ejecutarla y el servidor puede capturarla, puedes probar a transmitirla con MoonX360. El rendimiento depende del PC y de la capacidad del decodificador de la Xbox 360.

### ¿Puedo transmitir juegos de PS4 con shadPS4?

Sí, si shadPS4 ejecuta el juego correctamente en tu PC y tu servidor de streaming puede capturarlo. La compatibilidad y el rendimiento dependen de shadPS4 y de tu PC.

### ¿Tengo que descargar los juegos en el PC?

Sí, si son juegos que requieren instalación local. Usa el método habitual de instalación de cada juego o lanzador.

### ¿Funcionan los logros de Steam o Epic Games?

Sí, siempre que el juego y su lanzador admitan logros y se cumplan sus requisitos habituales. El juego se ejecuta en el PC con tu cuenta de siempre.

### ¿Puedo jugar en línea?

Sí, si el juego de PC tiene multijugador en línea. El PC gestiona el juego y su conexión a internet. MoonX360 transmite la imagen y el audio y envía al PC los controles. No necesitas Xbox Live para usar MoonX360.

---

## Requisitos del PC y de la red

### ¿Necesito un PC potente?

El PC debe poder ejecutar el juego y codificar el video al mismo tiempo. Un PC modesto puede ser suficiente para juegos ligeros, títulos antiguos o algunos emuladores.

### ¿Necesito conexión a internet?

Para transmitir dentro de casa, el PC y la Xbox 360 deben poder comunicarse en la red local. Puedes transmitir sin acceso a internet. La descarga del software, el inicio de sesión en un lanzador y el juego en línea pueden requerir acceso a internet.

### ¿Puedo usar Wi-Fi?

Sí. Ethernet por cable suele ser más estable. La conexión inalámbrica de la Xbox 360 puede sufrir interferencias, pérdida de paquetes y variaciones de latencia.

Si usas Wi-Fi, empieza con el perfil **Wireless Stable**.

### ¿Puedo conectar la Xbox 360 directamente al PC por Ethernet?

Sí. Una conexión Ethernet directa o el uso compartido de la conexión a internet de Windows pueden funcionar si ambos dispositivos tienen direcciones IP válidas y pueden comunicarse.

Al añadir el PC manualmente, escribe la dirección IP de la interfaz Ethernet del PC.

### ¿Qué ajustes debería usar?

Usa estos perfiles como punto de partida. Los nombres de la tabla corresponden a la interfaz en inglés.

| Perfil | Resolución | FPS objetivo | Tasa de bits |
| --- | --- | --- | --- |
| Smooth (predeterminado) | 1280×720 | 60 | 6 Mbps |
| Maximum Quality | 1280×720 | 60 | 10 Mbps |
| Standard | 1280×720 | 30 | 6 Mbps |
| Performance | 960×540 | 60 | 3 Mbps |
| Wireless Stable | 854×480 | 60 | 2 Mbps |
| CRT | 640×480 | 60 | 3 Mbps |
| 1080p (Experimental) | 1920×1080 | 30 | 8 Mbps |

Si ves tirones, baja la tasa de bits o los FPS. Los FPS indicados son el objetivo del perfil. La consola puede tardar más de lo previsto en decodificar y mostrar algunos fotogramas.

---

## Instalación

### ¿Necesito una Xbox 360 modificada?

Sí. Necesitas una Xbox 360 capaz de ejecutar aplicaciones caseras en formato XEX, como una consola con RGH o JTAG.

### ¿Puedo instalar MoonX360 en Aurora?

Sí. Descarga `default.xex` de la última versión estable, cópialo a una carpeta con permiso de escritura en el disco duro o la memoria USB de la consola y ejecútalo desde Aurora, FSD o XeXMenu. Puedes seguir usando tu dashboard habitual.

### ¿Dónde descargo el dashboard de MoonX360?

La interfaz de MoonX360 viene incluida en **`default.xex`**. [Descargar `default.xex`](https://github.com/fransley2/MoonX360/releases/latest)

### ¿Qué archivos debo descargar de la página de la versión?

Para una instalación estable, descarga los archivos `default.xex` y `SHA256SUMS.txt` de la [última versión estable](https://github.com/fransley2/MoonX360/releases/latest). Antes de copiar el ejecutable a la consola, compara su hash SHA-256 con el valor de `default.xex` en `SHA256SUMS.txt`. Si también ves un ZIP marcado como versión preliminar, usa los archivos individuales para la instalación estable.

### ¿MoonX360 necesita Xbox Live?

No. MoonX360 puede transmitir y enviar los controles a través de la red local.

### ¿Funciona en una Xbox 360 vetada de Xbox Live?

Sí, siempre que la consola pueda ejecutar la aplicación y conectarse al PC. MoonX360 funciona sin iniciar sesión en Xbox Live.

---

## Mandos, ratón y teclado

### ¿Funciona mi mando de Xbox 360 en los juegos de PC?

Sí, si el servidor de streaming y su sistema de mandos virtuales están configurados. La Xbox 360 lee las pulsaciones del mando y las envía al servidor por la red. El servidor las aplica a un mando virtual en el PC.

### ¿Por qué el juego no detecta mi mando?

Sunshine o el servidor compatible que uses debe crear un mando virtual en el PC. Si el juego no lo detecta, revisa primero el servidor.

1. Activa la entrada de mandos en Sunshine o en tu servidor compatible.
2. Comprueba que el controlador virtual que requiere tu versión y configuración del servidor esté instalado y funcione. Algunas configuraciones de Sunshine usan ViGEmBus. Consulta los [ajustes de mandos de Sunshine (en inglés)](https://docs.lizardbyte.dev/projects/sunshine/latest/md_docs_2configuration.html#gamepad) para identificar el controlador que usa tu instalación.
3. Selecciona la emulación Xbox 360/XInput si el servidor la ofrece.
4. Vuelve a conectarte y abre `joy.cpl` en el PC con la transmisión activa. Comprueba si aparece el mando virtual y si responde a las pulsaciones.

### ¿La Xbox 360 actúa como receptor inalámbrico para el PC?

MoonX360 envía las pulsaciones por la red. El mando se comunica con la Xbox 360 y el servidor de streaming crea el mando virtual en el PC.

### ¿Puedo controlar el escritorio de Windows?

Sí. El modo de escritorio incluye emulación de ratón y el teclado en pantalla **Smart Keyboard**. Abre el menú rápido para activar o desactivar **Modo ratón** (**Mouse mode**) o abrir el teclado durante la transmisión.

### ¿Cómo abro el menú rápido?

Durante la transmisión, pulsa **Back + L3**. L3 es el clic de la palanca analógica izquierda. Desde el menú rápido puedes reanudar la transmisión, actualizar la imagen, abrir el panel de rendimiento o desconectarte y volver a MoonX360.

---

## CRT y juegos retro

### ¿MoonX360 es compatible con televisores CRT de 4:3?

Sí. El perfil **CRT** transmite a 640×480 y **Imagen CRT** (**CRT Picture**) permite ajustar la relación de aspecto, el filtro de escalado, la posición de la imagen y el área segura de la interfaz. Si el cable y la pantalla admiten 480p, puedes seleccionar 480p en los ajustes de video de la Xbox 360. El perfil de streaming controla la resolución de la imagen recibida.

### ¿Puedo usar 240p?

Sí, de forma experimental en instalaciones compatibles. El perfil **CRT Retro (240p)** requiere que la consola esté configurada con salida SD a 480i/60 Hz y que DashLaunch tenga cargado un plugin externo `t240.xex` compatible (v2.0 o posterior). Si 240p no está disponible, usa el perfil **CRT** estándar.

### ¿Puedo usar resoluciones personalizadas como 2560×240?

El modo experimental de 240p documentado es **640×240**. Una resolución personalizada en el PC cambia la imagen que se captura. La Xbox 360 y su configuración de video determinan la señal de salida.

### ¿Qué tasa de bits debería usar con un CRT?

Empieza con el perfil **CRT** a **640×480 / 60 FPS / 3 Mbps**. Puedes probar 5 Mbps o más si la consola mantiene estables la decodificación y el ritmo de los fotogramas. Si aparecen tirones, vuelve a la tasa anterior.

---

## Rendimiento y solución de problemas

### ¿Hay retardo en los controles?

Sí. La latencia depende de la captura, la codificación, la red, la decodificación y la presentación en pantalla. Usar Ethernet y reducir la tasa de bits puede ayudar.

### ¿Por qué veo tirones?

Las causas posibles incluyen una tasa de bits demasiado alta para la consola, pérdida de paquetes, latencia variable de la red, demoras en el PC al capturar o codificar, y problemas de decodificación o de presentación en la consola.

Prueba una tasa de bits más baja o el perfil **Standard** a 720p30. Después revisa los datos d**Panel de rendimiento** (**Performance Overlay**).

### ¿Por qué veo bloques o colores incorrectos?

Los errores de decodificación o los fotogramas de referencia dañados pueden producir artefactos de H.264. MoonX360 incluye mejoras en el filtrado H.264 y en la recuperación de video, aunque todavía pueden aparecer fallos de imagen.

Prueba la última versión con una tasa de bits más baja. Si el problema continúa, indica qué servidor usas, la resolución, los FPS, la tasa de bits y el ajuste **Fast Decode**.

### ¿Por qué Steam se abre en el PC?

Steam se ejecuta en el PC y MoonX360 muestra la imagen capturada en la Xbox 360. Si Steam se abre en el PC y la consola no muestra el video, comprueba qué pantalla está capturando tu servidor.

### ¿Por qué el video se ve estirado, recortado o fuera de la pantalla completa?

Revisa la resolución de la pantalla del PC, la pantalla que captura el servidor, la relación de aspecto de la salida de la Xbox 360, los ajustes de **Imagen CRT** (**CRT Picture**) y la relación de aspecto de la resolución de streaming. Si usas una pantalla virtual, confirma que el servidor esté capturando esa pantalla.

### ¿Por qué la imagen se queda congelada en el primer fotograma mientras funcionan el audio y los controles?

El problema puede estar en la recepción de paquetes de video, en el inicio del decodificador o en la presentación de los fotogramas. Prueba un perfil de menor resolución, vuelve a conectarte y compara el resultado con otro cliente compatible.

Si continúa, anota los datos del **Panel de rendimiento** y explica si también ocurre con el otro cliente.

### ¿Qué significa «This legacy host has no saved server identity»?

MoonX360 no tiene guardada la identidad del servidor para esa entrada antigua. Vuelve a conectarte a la dirección conocida del PC o selecciónalo de nuevo en **Mis equipos** (**My Computers**) para actualizar la entrada. Si aparece **Vincular equipo** (**Pair with host**), sigue las instrucciones del PIN. Al actualizar MoonX360, conserva la carpeta `moonlight/` y los certificados de vinculación.

---

## Android y juego en la nube

### ¿Puedo usar mi teléfono Android en lugar de un PC?

La configuración habitual de MoonX360 requiere un servidor compatible con GameStream, como Sunshine. Moonlight en Android recibe transmisiones de un servidor compatible. Para enviar la pantalla de Android a MoonX360 haría falta una aplicación de servidor compatible aparte.

### ¿Existe Moonlight para Android o iPhone?

Sí. [Moonlight](https://moonlight-stream.org/) ofrece clientes para Android e iOS que reciben transmisiones desde un PC.

### ¿Puedo usar GeForce NOW o Xbox Cloud Gaming directamente en MoonX360?

MoonX360 está diseñado para recibir video de tu PC y no tiene integración directa con esos servicios. Si accedes a un servicio en la nube desde tu PC y transmites esa pantalla, puedes encontrar latencia adicional y limitaciones de compatibilidad o del servicio.

---

## Actualizaciones y soporte

### ¿Cómo actualizo MoonX360?

1. Descarga `default.xex` y verifica su hash siguiendo los pasos de [Instalación](#instalación).
2. Haz una copia de seguridad de tus archivos actuales.
3. Sustituye el ejecutable anterior.
4. Conserva la carpeta `moonlight/` para mantener los equipos guardados, los perfiles y los datos de vinculación.

### ¿Dónde puedo informar de un problema?

[Abre una incidencia en GitHub](https://github.com/fransley2/MoonX360/issues) e incluye estos datos.

- Modelo de Xbox 360.
- Versión de MoonX360.
- Nombre y versión del servidor de streaming.
- Conexión por Wi-Fi o Ethernet.
- Resolución, FPS y tasa de bits.
- Ajuste **Fast Decode**.
- Descripción del problema.
- Registros, capturas o video si están disponibles.

### ¿Sigue en desarrollo MoonX360?

Sí. MoonX360 es un proyecto casero experimental en desarrollo. Se siguen probando funciones y mejoras de rendimiento dentro de los límites del hardware de Xbox 360.

### ¿MoonX360 se basa en Xenonlight?

Sí. MoonX360 se basa en [Xenonlight](https://github.com/roderick-neuhoff/Xenonlight) y [Moonlight](https://moonlight-stream.org/). Consulta el [README](../README.md#credits--license) para ver los créditos y la licencia.

---

*MoonX360 es un proyecto casero independiente sin afiliación con Microsoft, Xbox, Valve ni NVIDIA.*
