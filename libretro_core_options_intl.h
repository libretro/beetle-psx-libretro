#ifndef LIBRETRO_CORE_OPTIONS_INTL_H__
#define LIBRETRO_CORE_OPTIONS_INTL_H__

#if defined(_MSC_VER) && (_MSC_VER >= 1500 && _MSC_VER < 1900)
/* https://support.microsoft.com/en-us/kb/980263 */
#pragma execution_character_set("utf-8")
#pragma warning(disable:4566)
#endif

#include <libretro.h>

#include "libretro_options.h"

/*
 ********************************
 * VERSION: 1.3
 ********************************
 *
 * - 1.3: Move translations to libretro_core_options_intl.h
 *        - libretro_core_options_intl.h includes BOM and utf-8
 *          fix for MSVC 2010-2013
 *        - Added HAVE_NO_LANGEXTRA flag to disable translations
 *          on platforms/compilers without BOM support
 * - 1.2: Use core options v1 interface when
 *        RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION is >= 1
 *        (previously required RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION == 1)
 * - 1.1: Support generation of core options v0 retro_core_option_value
 *        arrays containing options with a single value
 * - 1.0: First commit
*/

#ifdef __cplusplus
extern "C" {
#endif

/*
 ********************************
 * Core Option Definitions
 ********************************
*/

/* RETRO_LANGUAGE_JAPANESE */

/* RETRO_LANGUAGE_FRENCH */

/* RETRO_LANGUAGE_SPANISH */

struct retro_core_option_definition option_defs_es[] = {
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES) || defined(HAVE_VULKAN)
   {
      BEETLE_OPT(renderer),
      "Renderizador (es necesario reiniciar)",
      "Selecciona el renderizador de vídeo. El renderizador por software es el más preciso, pero sus requisitos son muy elevados si se ejecuta a una resolución interna superior a la de la GPU. Los renderizadores por hardware son menos precisos, pero tienen un mejor rendimiento al usar resoluciones internas superiores y permiten varias mejoras gráficas. «Hardware (Automático)» seleccionará automáticamente el renderizador Vulkan u OpenGL según el controlador de vídeo actual del front-end de libretro. Si el controlador no es compatible con Vulkan o con OpenGL 3.3, se utilizará el renderizador por software como reserva del núcleo.",
      {
         { "hardware",    "Hardware (automático)" },
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         { "hardware_gl", "Hardware (OpenGL)" },
#endif
#if defined(HAVE_VULKAN)
         { "hardware_vk", "Hardware (Vulkan)" },
#endif
         { "software",    "Software" },
         { NULL, NULL },
      },
      "hardware"
   },
   {
      BEETLE_OPT(renderer_software_fb),
      "Framebuffer por software",
      "Permite emular con precisión los efectos del framebuffer (p. ej.: desenfoque de movimiento, la espiral de batallas de FF7) al utilizar los renderizadores por hardware ejecutando una copia del renderizador por software a resolución nativa en un segundo plano. Al desactivar esta opción se omitirán estas operaciones (OpenGL) o se renderizarán en la GPU (Vulkan). Desactivar esta opción puede mejorar el rendimiento a costa de generar errores gráficos graves. En caso de duda, deja esta opción activada.",
      {
         { "enabled",  NULL },
         { "disabled", NULL },
         { NULL, NULL },
      },
      "enabled"
   },
#endif
   {
      BEETLE_OPT(internal_resolution),
      "Resolución interna de la GPU",
      "Ajusta el multiplicador de la resolución interna. Toda resolución superior a «1x (Nativa)» mejorarán la fidelidad de los modelos 3D a costa de aumentar los requisitos de rendimiento. Los elementos 2D no suelen verse afectados por este ajuste.",
      {
         { "1x(native)", "1x (nativa)" },
         { "2x",         NULL },
         { "4x",         NULL },
         { "8x",         NULL },
         { "16x",        NULL },
         { NULL, NULL },
      },
      "1x(native)"
   },
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
   {
      BEETLE_OPT(depth),
      "Profundidad interna de colores",
      "Ajusta la profundidad interna de colores. Una mayor profundidad puede reducir el efecto de bandas de color sin tener que recurrir al efecto de tramado. 16 bpp simula el hardware original, pero puede generar bandas de color si la opción de tramado no está activada. Se recomienda desactivar la opción «Tramado» al seleccionar 32 bpp en esta opción.",
      {
         { "16bpp(native)", "16 bpp (nativa)" },
         { "32bpp",         "32 bpp" },
         { NULL, NULL },
      },
      "16bpp(native)"
   },
   // Sort of, it's more like 15-bit high color and 24-bit true color for visible output. The alpha channel is used for mask bit. Vulkan renderer uses ABGR1555_555 for 31 bits internal? FMVs are always 24-bit on all renderers like original hardware (BGR888, no alpha)
#endif
   {
      BEETLE_OPT(dither_mode),
      "Tramado",
      "Ajusta la configuración del tramado. «1x (Nativo)» simula el tramado nativo a baja resolución que utilizaba el hardware original para suavizar los defectos de bandas de color visibles en la profundidad de colores nativa. «Resolución interna» cambia la escala del tramado a la resolución interna configurada para producir un resultado más limpio. Se recomienda desactivar esta opción al utilizar una profundidad de colores de 32 bpp. Nota: Activar esta opción en Vulkan desactivará la reducción de colores a la profundidad nativa y activará una profundidad interna superior.",
      {
         { "1x(native)",          "1x (nativo)" },
         { "internal resolution", "Resolución interna" },
         { "disabled",            NULL },
         { NULL, NULL },
      },
      "1x(native)"
   },
#ifdef HAVE_VULKAN
   {
      BEETLE_OPT(scaled_uv_offset),
      "Compensación de UV de texturas",
      "Muestrea las texturas de los polígonos 3D con una compensación para resoluciones superiores a la nativa. Reduce los salientes en las texturas, pero puede producir fallos gráficos accidentales.",
      {
         { "enabled",  NULL },
         { "disabled", NULL },
         { NULL, NULL },
      },
      "enabled"
   },
#endif
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES) || defined(HAVE_VULKAN)
   {
      BEETLE_OPT(filter),
      "Filtrado de texturas",
      "Selecciona el método de filtrado de texturas. «Más cercano» simula el hardware original. «Bilineal» y «De tres puntos» son filtros de suavizado que reducen la pixelación difuminando la imagen. «SABR», «xBR» y «JINC2» son filtros de escalado que podrían mejorar la fidelidad/definición de las texturas a costa de aumentar los requisitos de rendimiento. Solo funciona con los renderizadores por hardware.",
      {
         { "nearest",  "Más cercano" },
         { "SABR",     NULL },
         { "xBR",      NULL },
         { "bilinear", "Bilineal" },
         { "3-point",  "De tres puntos" },
         { "JINC2",    NULL },
         { NULL, NULL },
      },
      "nearest"
   },
#ifdef HAVE_VULKAN
   {
      BEETLE_OPT(filter_exclude_sprite),
      "No filtrar sprites",
      "No aplicará el filtrado de texturas a los sprites. Evita los bordes en varios juegos con fondos renderizados en 2D. Se recomienda activar también la opción de Suavizado adaptativo u otro efecto de posprocesado para conseguir el mejor efecto.",
      {
         { "disable", NULL },
         { "opaque", "Solo sprites opacos" },
         { "all", "Sprites opacos y semitransparentes" },
         { NULL, NULL },
      },
      "disable"
   },
   {
      BEETLE_OPT(filter_exclude_2d_polygon),
      "No filtrar polígonos 2D",
      "No aplicará el filtrado de texturas a los polígonos en 2D. Estos son detectados mediante un proceso heurístico, por lo que puede haber fallos visuales. Se recomienda activar también la opción de Suavizado adaptativo u otro efecto de posprocesado para conseguir el mejor efecto.",
      {
         { "disable", NULL },
         { "opaque", "Solo polígonos opacos" },
         { "all", "Polígonos opacos y semitransparentes" },
         { NULL, NULL },
      },
      "disable"
   },
#endif
#endif
#ifdef HAVE_VULKAN
   {
      BEETLE_OPT(adaptive_smoothing),
      "Suavizado adaptativo",
      "Suaviza los gráficos y elementos de interfaz en 2D sin difuminar los objetos renderizados en 3D. Solo funciona con el renderizador Vulkan.",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(super_sampling),
      "Supersampleado (reducción a resolución nativa)",
      "Reduce el contenido renderizado de una resolución interna superior a la nativa. Al combinar este efecto con multiplicadores para aumentar la resolución interna, los juegos pueden mostrarse con objetos 3D con bordes suavizados en la resolución nativa. Genera los mejores resultados en aquellos títulos que combinan elementos 2D y 3D (p. ej.: personajes 3D sobre fondos prerenderizados) y acompañado de shaders de simulación de CRT. Solo funciona con el renderizador Vulkan. Nota: Se recomienda desactivar el tramado al utilizar esta opción.",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(msaa),
      "Suavizado de bordes MSAA",
      "Ajusta el MSAA que se aplicará al contenido renderizado. Mejora el aspecto de los objetos 3D. Solo funciona con el renderizador Vulkan.",
      {
         { "1x",  "1x (predeterminado)" },
         { "2x",  NULL },
         { "4x",  NULL },
         { "8x",  NULL },
         { "16x", NULL },
         { NULL, NULL },
      },
      "1x"
   },
   {
      BEETLE_OPT(mdec_yuv),
      "Filtro cromático YUV para MDEC",
      "Mejora la calidad de la reproducción de secuencias FMV reduciendo los artefactos de los «macrobloques» (cuadrados y bordes de sierra). Solo funciona con el renderizador Vulkan.",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(track_textures),
      "Rastreo de texturas",
      "Necesario para el volcado y sustitución de texturas. Probablemente provocará bloqueos en la mayoría de juegos.",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
#ifdef TEXTURE_DUMPING_ENABLED
   {
      BEETLE_OPT(dump_textures),
      "Volcar texturas",
      "Vuelca las texturas que se utilicen a <cd>-texture-dump/",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
#endif
   {
      BEETLE_OPT(replace_textures),
      "Sustituir texturas",
      "Sustituye las texturas con las versiones en alta definición que se encuentren en <cd>-texture-replacements/",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
#endif
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
   {
      BEETLE_OPT(wireframe),
      "Modo de mallas (depuración)",
      "Renderiza solo las mallas de los objetos 3D, sin mostrar sus texturas o sombreado. Solo funciona con el renderizador por hardware de OpenGL. Nota: Esta opción solo se utiliza para depuración y debería mantenerse desactivada por norma general.",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
#endif
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES) || defined(HAVE_VULKAN)
   {
      BEETLE_OPT(display_vram),
      "Mostrar la VRAM completa (depuración)",
      "Visualiza toda la VRAM emulada.  Solo funciona con los renderizadores por hardware de OpenGL y Vulkan. Nota: Esta opción solo se utiliza para depuración y debería mantenerse desactivada por norma general.",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
#endif
   {
      BEETLE_OPT(pgxp_mode),
      "Modo de operación del PGXP",
      "Renderiza los objetos 3D con una precisión subpíxel, utilizando coordenadas de vértices de coma fija para minimizar la distorsión y temblores en objetos 3D que tenía el hardware original. «Solo en memoria» apenas da problemas de compatibilidad y se recomienda su uso general. «Memoria + CPU (inestable)» puede reducir aún más los temblores, pero tiene unos requisitos de rendimiento elevados y puede provocar fallos en la geometría.",
      {
         { "disabled",     NULL },
         { "memory only",  "Solo en memoria" },
         { "memory + CPU", "Memoria + CPU (inestable)" },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(pgxp_2d_tol),
      "Tolerancia a la geometría 2D del PGXP",
      "Oculta los errores más evidentes de las operaciones del PGXP: este valor especifica la tolerancia con la que se conservarán los valores del PGXP en aquellas geometrías que no dispongan de la información de profundidad necesaria.",
      {
         { "disabled", NULL },
         { "0px", NULL },
         { "1px", NULL },
         { "2px", NULL },
         { "3px", NULL },
         { "4px", NULL },
         { "5px", NULL },
         { "6px", NULL },
         { "7px", NULL },
         { "8px", NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(pgxp_nclip),
      "Selección de primitivos del PGXP",
      "Utiliza la implementación de NCLIP del PGXP. Mejora el aspecto visual tapando los agujeros de aquellas geometrías que tengan coordenadas PGXP. Provoca cuelgues en ciertos juegos y circunstancias.",
      {
         { "disabled", NULL },
         { "enabled", NULL },
         { NULL, NULL },
      },
      "disabled"
   },
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES) || defined(HAVE_VULKAN)
   {
      BEETLE_OPT(pgxp_vertex),
      "Caché de vértices del PGXP",
      "Permite que aquellas posiciones de vértices mejoradas por el PGXP sean cacheadas para reutilizarlas al dibujar polígonos. Puede mejorar la alineación de los objetos y reducir los bordes, pero los falsos positivos a la hora de recurrir a la caché pueden producir defectos visuales. Actualmente se recomienda desactivar esta opción. Solo se aplicará cuando se active el modo de operación del PGXP. Solo funciona con los renderizadores por hardware.",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(pgxp_texture),
      "Texturizado fiel a la perspectiva del PGXP",
      "Sustituye el mapeado de texturas afín nativo de PSX con un mapeado de texturas fiel a la perspectiva. Elimina las distorsiones posicionales y la deformación de texturas, produciendo texturas correctamente alineadas. Solo se aplicará cuando se active el modo de operación del PGXP. Solo funciona con los renderizadores por hardware.",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
#endif
   {
      BEETLE_OPT(display_internal_fps),
      "Mostrar FPS internos",
      "Muestra la velocidad de fotogramas interna con la que el sistema PlayStation emulado renderiza el contenido. Nota: Es necesario activar las notificaciones en pantalla en el front-end libretro.",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(line_render),
      "Hack para el efecto «Line-to-Quad»",
      "Selecciona el método de ejecución del hack para convertir líneas a rectángulos. Ciertos juegos (Doom, Hexen, Soul Blade, etcétera) dibujan líneas horizontales estirando triángulos de un píxel de altura a lo largo de la pantalla, que luego son rasterizados como una hilera de píxeles en el hardware original. Este hack detecta esos triángulos y los convierte en rectángulos según sea necesario, permitiendo que se muestren correctamente en los renderizadores por hardware y en resoluciones internas superiores a la nativa. La opción «Agresivo» es necesaria en ciertos títulos (p. ej.: Dark Forces, Duke Nukem) pero puede producir defectos visuales en otros. En caso de duda, seleccionar «Predeterminado».",
      {
         { "default",    "Predeterminado" },
         { "aggressive", "Agresivo" },
         { "disabled",   NULL },
         { NULL, NULL },
      },
      "default"
   },
   {
      BEETLE_OPT(frame_duping),
      "Duplicado de fotogramas (mayor velocidad)",
      "Al activar esta opción (y si es compatible con el front-end de libretro), aumenta levemente el rendimiento al indicar al front-end que repita el fotograma anterior si el núcleo no muestra nada nuevo.",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
#if defined(HAVE_LIGHTREC)
   {
      BEETLE_OPT(cpu_dynarec),
      "Dynarec de CPU",
      "Recompila de forma dinámica las instrucciones de la CPU a instrucciones nativas. Es más rápido que el intérprete, pero los intervalos de la CPU son menos precisos y puede provocar fallos.",
      {
         { "disabled", "Desactivada (intérprete Beetle)" },
         { "execute",  "Máximo rendimiento" },
         { "execute_one",  "Comprobación del intervalo entre ciclos" },
         { "run_interpreter", "Intérprete Lightrec" },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(dynarec_invalidate),
      "Invalidación de código del dynarec",
      "Algunos juegos necesitan una invalidación completa y otros solo necesitan invalidar la DMA.",
      {
         { "full", "Completa" },
         { "dma",  "Solo DMA (algo más rápida)" },
         { NULL, NULL },
      },
      "full"
   },
   {
      BEETLE_OPT(dynarec_eventcycles),
      "Ciclos de eventos de DMA/GPU del dynarec",
      "Indica el número máximo de ciclos ejecutados por la CPU antes de comprobar la DMA o la GPU. Un valor superior será más rápido. Tiene menos repercusión en el intérprete Beetle que en el dynarec.",
      {
         { "128", "128 (valor predeterminado)" },
         { "256",  NULL },
         { "384",  NULL },
         { "512",  NULL },
         { "640",  NULL },
         { "768",  NULL },
         { "896",  NULL },
         { "1024",  NULL },
         { NULL, NULL },
      },
      "128"
   },
#endif
   {
      BEETLE_OPT(cpu_freq_scale),
      "Escalado de frecuencia de la CPU («overclocking»)",
      "Aumenta (o disminuye) la velocidad del reloj emulado de la CPU de PSX. Una velocidad superior puede eliminar las ralentizaciones y mejorar la velocidad de fotogramas en ciertos juegos a costa de aumentar los requisitos de rendimiento. Ten en cuenta que ciertos juegos incluyen un limitador de fotogramas interno y no ganarán beneficio alguno al aumentar la velocidad. Puede provocar que ciertos efectos vayan más rápido de lo normal en ciertos juegos.",
      {
         { "50%",           NULL },
         { "60%",           NULL },
         { "70%",           NULL },
         { "80%",           NULL },
         { "90%",           NULL },
         { "100%(native)", "100% (nativo)" },
         { "110%",          NULL },
         { "120%",          NULL },
         { "130%",          NULL },
         { "140%",          NULL },
         { "150%",          NULL },
         { "160%",          NULL },
         { "170%",          NULL },
         { "180%",          NULL },
         { "190%",          NULL },
         { "200%",          NULL },
         { "210%",          NULL },
         { "220%",          NULL },
         { "230%",          NULL },
         { "240%",          NULL },
         { "250%",          NULL },
         { "260%",          NULL },
         { "270%",          NULL },
         { "280%",          NULL },
         { "290%",          NULL },
         { "300%",          NULL },
         { "310%",          NULL },
         { "320%",          NULL },
         { "330%",          NULL },
         { "340%",          NULL },
         { "350%",          NULL },
         { "360%",          NULL },
         { "370%",          NULL },
         { "380%",          NULL },
         { "390%",          NULL },
         { "400%",          NULL },
         { "410%",          NULL },
         { "420%",          NULL },
         { "430%",          NULL },
         { "440%",          NULL },
         { "450%",          NULL },
         { "460%",          NULL },
         { "470%",          NULL },
         { "480%",          NULL },
         { "490%",          NULL },
         { "500%",          NULL },
         { "510%",          NULL },
         { "520%",          NULL },
         { "530%",          NULL },
         { "540%",          NULL },
         { "550%",          NULL },
         { "560%",          NULL },
         { "570%",          NULL },
         { "580%",          NULL },
         { "590%",          NULL },
         { "600%",          NULL },
         { "610%",          NULL },
         { "620%",          NULL },
         { "630%",          NULL },
         { "640%",          NULL },
         { "650%",          NULL },
         { "660%",          NULL },
         { "670%",          NULL },
         { "680%",          NULL },
         { "690%",          NULL },
         { "700%",          NULL },
         { "710%",          NULL },
         { "720%",          NULL },
         { "730%",          NULL },
         { "740%",          NULL },
         { "750%",          NULL },
         { NULL, NULL },
      },
      "100%(native)"
   },
   {
      BEETLE_OPT(gte_overclock),
      "Overclockear GTE",
      "Ralentiza todas las operaciones emuladas del GTE (el coprocesador de la CPU para gráficos 3D) a una latencia constante de un ciclo. Aquellos juegos que utilicen en gran medida el GTE pueden mejorar su velocidad de fotogramas y la estabilidad en la duración de los mismos.",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(gpu_overclock),
      "Overclockear rasterizador de la GPU",
      "Aumenta la velocidad del rasterizador 2D incluido en la GPU emulada de PSX. No mejora el renderizado 3D y su efecto suele ser limitado.",
      {
         { "1x(native)", "1x (nativa)" },
         { "2x",         NULL },
         { "4x",         NULL },
         { "8x",         NULL },
         { "16x",        NULL },
         { "32x",        NULL },
         { NULL, NULL },
      },
      "1x(native)"
   },
   {
      BEETLE_OPT(skip_bios),
      "Omitir BIOS",
      "Omite la animación de arranque de la BIOS de PlayStation que suele aparecer al cargar un contenido. Nota: Esta opción puede generar problemas de compatibilidad en varios juegos (juegos PAL con protección anticopia, Saga Frontier, etc.).",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(core_timing_fps),
      "Barrido de señal según núcleo",
      "Ajusta el barrido de señal que indicará el núcleo al front-end. «Cambiar automáticamente» permite que el núcleo cambie entre los barridos progresivo y entrelazado, pero pueden provocar reinicios en los controladores de vídeo y audio del front-end.",
      {
         { "force_progressive", "Barrido progresivo (predeterminado)" },
         { "force_interlaced",  "Forzar barrido entrelazado" },
         { "auto_toggle", "Cambiar automáticamente" },
      },
      "force_progressive"
   },
   {
      BEETLE_OPT(aspect_ratio),
      "Relación de aspecto del núcleo",
      "Ajusta la relación de aspecto del núcleo. Este ajuste será ignorado si se activan las opciones «Hack para pantallas panorámicas» o «Mostrar la VRAM completa».",
      {
         { "corrected", "Corregida" },
         { "uncorrected", "Sin corregir" },
         { "4:3",  "Forzar 4:3" },
         { "ntsc", "Forzar NTSC" },
      },
      "corrected"
   },
   {
      BEETLE_OPT(widescreen_hack),
      "Hack para pantallas panorámicas",
      "Renderiza los contenidos 3D en formato anamórfico y hace que el framebuffer emulado tenga una relación de aspecto panorámica. Produce los mejores resultados con juegos hechos completamente en 3D. Los elementos 2D se estirarán horizontalmente y podrían dejar de estar alineados.",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(widescreen_hack_aspect_ratio),
      "Relación de aspecto del hack para pantallas panorámicas",
      "Indica la relación de aspecto usada por el hack para pantallas panorámicas.",
      {
         { "16:10", NULL },
         { "16:9",  NULL },
         { "21:9",  NULL }, // 64:27
         { "32:9",  NULL },
         { NULL,    NULL },
      },
      "16:9"
   },
   {
      BEETLE_OPT(pal_video_timing_override),
      "Anular sincronía de vídeo PAL (europea)",
      "Debido a la diferencia entre estándares, los juegos PAL suelen parecer más lentos que sus versiones NTSC, tanto estadounidenses como japonesas. Esta opción puede anular la velocidad de vídeo PAL para intentar ejecutar dichos juegos con la velocidad de fotogramas NTSC. Esta opción no produce efecto alguno en contenidos NTSC.",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(crop_overscan),
      "Recortar sobrebarrido horizontal",
      "Los renderizadores añaden márgenes horizontales de forma predeterminada (columnas negras a ambos lados de la imagen) para simular las mismas columnas negras que produce el hardware real de PSX en una señal de vídeo analógica. Esta opción elimina los márgenes horizontales.",
      {
         { "enabled",  NULL },
         { "disabled", NULL },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      BEETLE_OPT(image_crop),
      "Recorte adicional",
      "Al activar la opción «Recortar sobrebarrido horizontal», esta opción permite reducir el ancho de la imagen recortada usando un valor concreto en píxeles. Solo funciona con el renderizador por software.",
      {
         { "disabled", NULL },
         { "1px",      NULL },
         { "2px",      NULL },
         { "3px",      NULL },
         { "4px",      NULL },
         { "5px",      NULL },
         { "6px",      NULL },
         { "7px",      NULL },
         { "8px",      NULL },
         { "9px",      NULL },
         { "10px",     NULL },
         { "11px",     NULL },
         { "12px",     NULL },
         { "13px",     NULL },
         { "14px",     NULL },
         { "15px",     NULL },
         { "16px",     NULL },
         { "17px",     NULL },
         { "18px",     NULL },
         { "19px",     NULL },
         { "20px",     NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(image_offset),
      "Desplazar imagen recortada",
      "Al activar la opción «Recortar sobrebarrido horizontal», esta opción permite desplazar horizontalmente la imagen recortada hacia la derecha (valores positivos) o hacia la izquierda (valores negativos) usando un valor concreto en píxeles. Utilizar para corregir problemas de alineación. Solo funciona con el renderizador por software.",
      {
         { "-12px",    NULL },
         { "-11px",    NULL },
         { "-10px",    NULL },
         { "-9px",     NULL },
         { "-8px",     NULL },
         { "-7px",     NULL },
         { "-6px",     NULL },
         { "-5px",     NULL },
         { "-4px",     NULL },
         { "-3px",     NULL },
         { "-2px",     NULL },
         { "-1px",     NULL },
         { "disabled", NULL },
         { "+1px",     NULL },
         { "+2px",     NULL },
         { "+3px",     NULL },
         { "+4px",     NULL },
         { "+5px",     NULL },
         { "+6px",     NULL },
         { "+7px",     NULL },
         { "+8px",     NULL },
         { "+9px",     NULL },
         { "+10px",    NULL },
         { "+11px",    NULL },
         { "+12px",    NULL },
         { NULL, NULL },
      },
      "disabled"
   },
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES) || defined(HAVE_VULKAN)
   {
      BEETLE_OPT(image_offset_cycles),
      "Desplazar imagen horizontal (ciclos de GPU)",
      "Especifica el número de ciclos de GPU con los que desplazar la imagen. Los valores positivos mueven la imagen hacia la derecha y los negativos hacia la izquierda.  Solo funciona con los renderizadores por hardware.",
      {
         { "-40",      NULL },
         { "-39",      NULL },
         { "-38",      NULL },
         { "-37",      NULL },
         { "-36",      NULL },
         { "-35",      NULL },
         { "-34",      NULL },
         { "-33",      NULL },
         { "-32",      NULL },
         { "-31",      NULL },
         { "-30",      NULL },
         { "-29",      NULL },
         { "-28",      NULL },
         { "-27",      NULL },
         { "-26",      NULL },
         { "-25",      NULL },
         { "-24",      NULL },
         { "-23",      NULL },
         { "-22",      NULL },
         { "-21",      NULL },
         { "-20",      NULL },
         { "-19",      NULL },
         { "-18",      NULL },
         { "-17",      NULL },
         { "-16",      NULL },
         { "-15",      NULL },
         { "-14",      NULL },
         { "-13",      NULL },
         { "-12",      NULL },
         { "-11",      NULL },
         { "-10",      NULL },
         { "-9",       NULL },
         { "-8",       NULL },
         { "-7",       NULL },
         { "-6",       NULL },
         { "-5",       NULL },
         { "-4",       NULL },
         { "-3",       NULL },
         { "-2",       NULL },
         { "-1",       NULL },
         { "0",        NULL },
         { "+1",       NULL },
         { "+2",       NULL },
         { "+3",       NULL },
         { "+4",       NULL },
         { "+5",       NULL },
         { "+6",       NULL },
         { "+7",       NULL },
         { "+8",       NULL },
         { "+9",       NULL },
         { "+10",      NULL },
         { "+11",      NULL },
         { "+12",      NULL },
         { "+13",      NULL },
         { "+14",      NULL },
         { "+15",      NULL },
         { "+16",      NULL },
         { "+17",      NULL },
         { "+18",      NULL },
         { "+19",      NULL },
         { "+20",      NULL },
         { "+21",      NULL },
         { "+22",      NULL },
         { "+23",      NULL },
         { "+24",      NULL },
         { "+25",      NULL },
         { "+26",      NULL },
         { "+27",      NULL },
         { "+28",      NULL },
         { "+29",      NULL },
         { "+30",      NULL },
         { "+31",      NULL },
         { "+32",      NULL },
         { "+33",      NULL },
         { "+34",      NULL },
         { "+35",      NULL },
         { "+36",      NULL },
         { "+37",      NULL },
         { "+38",      NULL },
         { "+39",      NULL },
         { "+40",      NULL },
         { NULL, NULL},
      },
      "0"
   },
#endif
   {
      BEETLE_OPT(initial_scanline),
      "Línea de barrido inicial en formato NTSC",
      "Selecciona la primera línea de barrido que se mostrará al ejecutar contenidos NTSC. Un valor superior a cero reducirá la altura de las imágenes generadas recortando los píxeles de la parte superior. Puede servir para evitar el efecto letterboxing.",
      {
         { "0",  NULL },
         { "1",  NULL },
         { "2",  NULL },
         { "3",  NULL },
         { "4",  NULL },
         { "5",  NULL },
         { "6",  NULL },
         { "7",  NULL },
         { "8",  NULL },
         { "9",  NULL },
         { "10", NULL },
         { "11", NULL },
         { "12", NULL },
         { "13", NULL },
         { "14", NULL },
         { "15", NULL },
         { "16", NULL },
         { "17", NULL },
         { "18", NULL },
         { "19", NULL },
         { "20", NULL },
         { "21", NULL },
         { "22", NULL },
         { "23", NULL },
         { "24", NULL },
         { "25", NULL },
         { "26", NULL },
         { "27", NULL },
         { "28", NULL },
         { "29", NULL },
         { "30", NULL },
         { "31", NULL },
         { "32", NULL },
         { "33", NULL },
         { "34", NULL },
         { "35", NULL },
         { "36", NULL },
         { "37", NULL },
         { "38", NULL },
         { "39", NULL },
         { "40", NULL },
         { NULL, NULL },
      },
      "0"
   },
   {
      BEETLE_OPT(last_scanline),
      "Línea de barrido final en formato NTSC",
      "Selecciona la última línea de barrido que se mostrará al ejecutar contenidos NTSC. Un valor superior inferior a 239 reducirá la altura de las imágenes generadas recortando los píxeles de la parte inferior. Puede servir para evitar el efecto letterboxing.",
      {
         { "210", NULL },
         { "211", NULL },
         { "212", NULL },
         { "213", NULL },
         { "214", NULL },
         { "215", NULL },
         { "216", NULL },
         { "217", NULL },
         { "218", NULL },
         { "219", NULL },
         { "220", NULL },
         { "221", NULL },
         { "222", NULL },
         { "223", NULL },
         { "224", NULL },
         { "225", NULL },
         { "226", NULL },
         { "227", NULL },
         { "228", NULL },
         { "229", NULL },
         { "230", NULL },
         { "231", NULL },
         { "232", NULL },
         { "233", NULL },
         { "234", NULL },
         { "235", NULL },
         { "236", NULL },
         { "237", NULL },
         { "238", NULL },
         { "239", NULL },
         { NULL, NULL },
      },
      "239"
   },
   {
      BEETLE_OPT(initial_scanline_pal),
      "Línea de barrido inicial en formato PAL",
      "Selecciona la primera línea de barrido que se mostrará al ejecutar contenidos PAL. Un valor superior a cero reducirá la altura de las imágenes generadas recortando los píxeles de la parte superior. Puede servir para evitar el efecto letterboxing.",
      {
         { "0",  NULL },
         { "1",  NULL },
         { "2",  NULL },
         { "3",  NULL },
         { "4",  NULL },
         { "5",  NULL },
         { "6",  NULL },
         { "7",  NULL },
         { "8",  NULL },
         { "9",  NULL },
         { "10", NULL },
         { "11", NULL },
         { "12", NULL },
         { "13", NULL },
         { "14", NULL },
         { "15", NULL },
         { "16", NULL },
         { "17", NULL },
         { "18", NULL },
         { "19", NULL },
         { "20", NULL },
         { "21", NULL },
         { "22", NULL },
         { "23", NULL },
         { "24", NULL },
         { "25", NULL },
         { "26", NULL },
         { "27", NULL },
         { "28", NULL },
         { "29", NULL },
         { "30", NULL },
         { "31", NULL },
         { "32", NULL },
         { "33", NULL },
         { "34", NULL },
         { "35", NULL },
         { "36", NULL },
         { "37", NULL },
         { "38", NULL },
         { "39", NULL },
         { "40", NULL },
         { NULL, NULL },
      },
      "0"
   },
   {
      BEETLE_OPT(last_scanline_pal),
      "Línea de barrido final en formato PAL",
      "Selecciona la última línea de barrido que se mostrará al ejecutar contenidos PAL. Un valor superior inferior a 287 reducirá la altura de las imágenes generadas recortando los píxeles de la parte inferior. Puede servir para evitar el efecto letterboxing.",
      {
         { "230", NULL },
         { "231", NULL },
         { "232", NULL },
         { "233", NULL },
         { "234", NULL },
         { "235", NULL },
         { "236", NULL },
         { "237", NULL },
         { "238", NULL },
         { "239", NULL },
         { "240", NULL },
         { "241", NULL },
         { "242", NULL },
         { "243", NULL },
         { "244", NULL },
         { "245", NULL },
         { "246", NULL },
         { "247", NULL },
         { "248", NULL },
         { "249", NULL },
         { "250", NULL },
         { "251", NULL },
         { "252", NULL },
         { "253", NULL },
         { "254", NULL },
         { "255", NULL },
         { "256", NULL },
         { "257", NULL },
         { "258", NULL },
         { "259", NULL },
         { "260", NULL },
         { "261", NULL },
         { "262", NULL },
         { "263", NULL },
         { "264", NULL },
         { "265", NULL },
         { "266", NULL },
         { "267", NULL },
         { "268", NULL },
         { "269", NULL },
         { "270", NULL },
         { "271", NULL },
         { "272", NULL },
         { "273", NULL },
         { "274", NULL },
         { "275", NULL },
         { "276", NULL },
         { "277", NULL },
         { "278", NULL },
         { "279", NULL },
         { "280", NULL },
         { "281", NULL },
         { "282", NULL },
         { "283", NULL },
         { "284", NULL },
         { "285", NULL },
         { "286", NULL },
         { "287", NULL },
         { NULL, NULL },
      },
      "287"
   },
#ifndef EMSCRIPTEN
   {
      BEETLE_OPT(cd_access_method),
      "Método de acceso al CD (es necesario reiniciar)",
      "Selecciona el método utilizado para leer datos de las imágenes de disco del contenido. «Sincrónico» simula el hardware original, «Asincrónico» puede reducir los tirones en aquellos dispositivos que tengan un almacenamiento lento y «Precacheado» carga la imagen de disco entera en memoria al ejecutar el contenido, lo que puede mejorar los tiempos de carga del juego a costa de retrasar el arranque inicial. «Precacheado» puede producir problemas en sistemas con poca RAM.",
      {
         { "sync",     "Sincrónico" },
         { "async",    "Asincrónico" },
         { "precache", "Precacheado" },
         { NULL, NULL },
      },
      "sync"
   },
#endif
   {
      BEETLE_OPT(cd_fastload),
      "Velocidad de lectura del CD",
      "Selecciona el multiplicador de velocidad de acceso al disco. Un valor superior a «2x (nativa)» puede reducir en gran medida los tiempos de carga de los juegos, pero produciendo errores de sincronía. Algunos juegos podrían no funcionar correctamente si este valor es superior a cierta cifra.",
      {
         { "2x(native)", "2x (nativa)" },
         { "4x",          NULL },
         { "6x",          NULL },
         { "8x",          NULL },
         { "10x",         NULL },
         { "12x",         NULL },
         { "14x",         NULL },
         { NULL, NULL },
      },
      "2x(native)"
   },
   {
      BEETLE_OPT(use_mednafen_memcard0_method),
      "Método de la Memory Card 0 (es necesario reiniciar)",
      "Selecciona el formato de datos guardados que se utilizará en la Memory Card 0. «Mednafen» puede mejorar la compatibilidad con la versión independiente de Mednafen. Al utilizar los archivos guardados con Beetle PSX, los archivos de Libretro (.srm) y Mednafen (.mcr) son idénticos internamente, así que pueden intercambiarse entre sí modificando sus nombres.",
      {
         { "libretro", "Libretro" },
         { "mednafen", "Mednafen" },
         { NULL, NULL },
      },
      "libretro"
   },
   {
      BEETLE_OPT(enable_memcard1),
      "Activar Memory Card 1 (es necesario reiniciar)",
      "Indica si se debe emular una segunda Memory Card en la ranura 1. Al desactivar esta opción, los juegos solo podrán acceder a la Memory Card de la ranura 0. Nota: algunos juegos necesitan que esta opción esté desactivada para funcionar correctamente (p. ej.: Codename Tenka). Nota: la Memory Card 1 utiliza el formato de guardado Mednafen (.mcr).",
      {
         { "enabled",  NULL },
         { "disabled", NULL },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      BEETLE_OPT(shared_memory_cards),
      "Memory Card compartidas (es necesario reiniciar)",
      "Al activar esta opción, todos los juegos utilizarán los mismos archivos de Memory Card para guardar y cargar partidas. Al desactivarla se utilizarán archivos de Memory Card independientes por cada elemento del contenido cargado. Nota: si la opción «Método de la Memory Card 0» está configurada como «Libretro», esta opción solo afectará a la Memory Card de la ranura 1.",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(analog_calibration),
      "Autocalibrar controles analógicos",
      "Cuando el dispositivo de entrada esté configurado como DualShock, mando analógico, joystick analógico o NeGcon, esta opción permitirá calibrar de forma dinámica las entradas analógicas. Se controlarán en tiempo real los valores de entrada máximos registrados para escalar las coordenadas analógicas que se transmitan al emulador. Esto sirve para juegos como Mega Man Legends 2, que esperan unos valores superiores a los que pueden transmitir los mandos modernos. Se recomienda que los sticks analógicos sean girados en toda su extensión nada más cargar un contenido para que el algoritmo de calibración funcione lo mejor posible.",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(analog_toggle),
      "Conmutador de modo analógico del DualShock",
      "Cuando el tipo de dispositivo de entrada sea DualShock, indica si el DualShock emulado puede alternar entre los modos digital y analógico, como hace el hardware original. Al desactivar esta opción el DualShock quedará fijado en el modo analógico y al activarla el DualShock podrá cambiar entre los modos digital y analógico manteniendo pulsada la combinación START+SELECT+L1+L2+R1+R2.",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(enable_multitap_port1),
      "Puerto 1: activar Multitap",
      "Activa el sistema de Multitap en el puerto 1.",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(enable_multitap_port2),
      "Puerto 2: activar Multitap",
      "Activa el sistema de Multitap en el puerto 2.",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(gun_input_mode),
      "Modo de entrada de pistolas de luz",
      "Indica si se debe usar una entrada de ratón o de pantalla táctil cuando el tipo de dispositivo esté configurado como «Guncon/G-Con 45» o «Justifier».",
      {
         { "lightgun",    "Pistola de luz/Ratón" },
         { "touchscreen", "Pantalla táctil" },
         { NULL, NULL },
      },
      "lightgun"
   },
   // Shouldn't the gun_input_mode just be Mouse vs. Touchscreen?
   {
      BEETLE_OPT(gun_cursor),
      "Cursor del ratón",
      "Selecciona el cursor que aparecerá en pantalla al usar los tipos de dispositivo de entrada «Guncon/G-Con 45» o «Justifier». Al desactivar esta opción se ocultará el punto de mira.",
      {
         { "cross", "Cruz" },
         { "dot",   "Punto" },
         { "off",   "Desactivado" },
         { NULL, NULL },
      },
      "cross"
   },
   {
      BEETLE_OPT(mouse_sensitivity),
      "Sensibilidad del ratón",
      "Cambia la respuesta del tipo de dispositivo de entrada «Mouse (ratón)».",
      {
         { "5%",   NULL },
         { "10%",  NULL },
         { "15%",  NULL },
         { "20%",  NULL },
         { "25%",  NULL },
         { "30%",  NULL },
         { "35%",  NULL },
         { "40%",  NULL },
         { "45%",  NULL },
         { "50%",  NULL },
         { "55%",  NULL },
         { "60%",  NULL },
         { "65%",  NULL },
         { "70%",  NULL },
         { "75%",  NULL },
         { "80%",  NULL },
         { "85%",  NULL },
         { "90%",  NULL },
         { "95%",  NULL },
         { "100%", NULL },
         { "105%", NULL },
         { "110%", NULL },
         { "115%", NULL },
         { "120%", NULL },
         { "125%", NULL },
         { "130%", NULL },
         { "135%", NULL },
         { "140%", NULL },
         { "145%", NULL },
         { "150%", NULL },
         { "155%", NULL },
         { "160%", NULL },
         { "165%", NULL },
         { "170%", NULL },
         { "175%", NULL },
         { "180%", NULL },
         { "185%", NULL },
         { "190%", NULL },
         { "195%", NULL },
         { "200%", NULL },
         { NULL, NULL },
      },
      "100%"
   },
   {
      BEETLE_OPT(negcon_response),
      "Respuesta al torcer el neGcon",
      "Especifica la respuesta del stick analógico izquierdo del RetroPad al simular la acción de torcer un dispositivo de entrada neGcon emulado. El desplazamiento del stick analógico puede ser asignado a la rotación del neGcon de forma lineal, cuadrática o cúbica. «Cuadrática» da una mayor precisión que «Lineal» en movimientos más sutiles. «Cúbica» aumenta más la precisión de los movimientos más sutiles, pero exagera los más largos. Nota: solo se recomienda usar «Lineal» con periféricos tipo volante de carreras. Los mandos convencionales gestionan la entrada analógica de una forma muy distinta al mecanismo de rotación del neGcon de tal forma que la asignación lineal sobreamplifica los movimientos sutiles, lo que afecta al control preciso. En la mayoría de casos, la opción «Cuadrática» produce la mejor aproximación posible al hardware real.",
      {
         { "linear",    "Lineal" },
         { "quadratic", "Cuadrática" },
         { "cubic",     "Cúbica" },
         { NULL, NULL },
      },
      "linear"
   },
   {
      BEETLE_OPT(negcon_deadzone),
      "Zona muerta al torcer el neGcon",
      "Ajusta la zona muerta del stick analógico izquierdo del RetroPad al simular la acción de torcer un dispositivo de entrada neGcon emulado. Sirve para evitar que el mando registre movimientos no realizados. Nota: la mayoría de juegos compatibles con neGcon incluyen opciones dentro del juego para ajustar un valor de zona muerta al torcerlo. Este valor debe configurarse como cero o neutral en todo momento para que no haya una pérdida de precisión. Cualquier ajuste que sea necesario solo debe aplicarse mediante esta opción del núcleo. Esta opción es especialmente importante cuando la opción «Respuesta al torcer el neGcon» esté configurada como «Cuadrática» o «Cúbica».",
      {
         { "0%",  NULL },
         { "5%",  NULL },
         { "10%", NULL },
         { "15%", NULL },
         { "20%", NULL },
         { "25%", NULL },
         { "30%", NULL },
         { NULL, NULL },
      },
      "0%"
   },
   {
      BEETLE_OPT(memcard_left_index),
      "Índice de la Memory Card izquierda",
      "Cambia la Memory Card que está cargada en la ranura izquierda. Esta opción solo funcionará si la opción «Método de la Memory Card 0» está configurada como «Mednafen». La Memory Card predeterminada es el índice 0.",
      {
         { "0",  NULL },
         { "1",  NULL },
         { "2",  NULL },
         { "3",  NULL },
         { "4",  NULL },
         { "5",  NULL },
         { "6",  NULL },
         { "7",  NULL },
         { "8",  NULL },
         { "9",  NULL },
         { "10",  NULL },
         { "11",  NULL },
         { "12",  NULL },
         { "13",  NULL },
         { "14",  NULL },
         { "15",  NULL },
         { "16",  NULL },
         { "17",  NULL },
         { "18",  NULL },
         { "19",  NULL },
         { "20",  NULL },
         { "21",  NULL },
         { "22",  NULL },
         { "23",  NULL },
         { "24",  NULL },
         { "25",  NULL },
         { "26",  NULL },
         { "27",  NULL },
         { "28",  NULL },
         { "29",  NULL },
         { "30",  NULL },
         { "31",  NULL },
         { "32",  NULL },
         { "33",  NULL },
         { "34",  NULL },
         { "35",  NULL },
         { "36",  NULL },
         { "37",  NULL },
         { "38",  NULL },
         { "39",  NULL },
         { "40",  NULL },
         { "41",  NULL },
         { "42",  NULL },
         { "43",  NULL },
         { "44",  NULL },
         { "45",  NULL },
         { "46",  NULL },
         { "47",  NULL },
         { "48",  NULL },
         { "49",  NULL },
         { "50",  NULL },
         { "51",  NULL },
         { "52",  NULL },
         { "53",  NULL },
         { "54",  NULL },
         { "55",  NULL },
         { "56",  NULL },
         { "57",  NULL },
         { "58",  NULL },
         { "59",  NULL },
         { "60",  NULL },
         { "61",  NULL },
         { "62",  NULL },
         { "63",  NULL },
         { NULL, NULL },
      },
      "0"
   },
   {
      BEETLE_OPT(memcard_right_index),
      "Índice de la Memory Card derecha",
      "Cambia la Memory Card que está cargada en la ranura derecha. Esta opción solo funcionará si la opción «Activar Memory Card 1» está activada. La Memory Card predeterminada es el índice 1.",
      {
         { "0",  NULL },
         { "1",  NULL },
         { "2",  NULL },
         { "3",  NULL },
         { "4",  NULL },
         { "5",  NULL },
         { "6",  NULL },
         { "7",  NULL },
         { "8",  NULL },
         { "9",  NULL },
         { "10",  NULL },
         { "11",  NULL },
         { "12",  NULL },
         { "13",  NULL },
         { "14",  NULL },
         { "15",  NULL },
         { "16",  NULL },
         { "17",  NULL },
         { "18",  NULL },
         { "19",  NULL },
         { "20",  NULL },
         { "21",  NULL },
         { "22",  NULL },
         { "23",  NULL },
         { "24",  NULL },
         { "25",  NULL },
         { "26",  NULL },
         { "27",  NULL },
         { "28",  NULL },
         { "29",  NULL },
         { "30",  NULL },
         { "31",  NULL },
         { "32",  NULL },
         { "33",  NULL },
         { "34",  NULL },
         { "35",  NULL },
         { "36",  NULL },
         { "37",  NULL },
         { "38",  NULL },
         { "39",  NULL },
         { "40",  NULL },
         { "41",  NULL },
         { "42",  NULL },
         { "43",  NULL },
         { "44",  NULL },
         { "45",  NULL },
         { "46",  NULL },
         { "47",  NULL },
         { "48",  NULL },
         { "49",  NULL },
         { "50",  NULL },
         { "51",  NULL },
         { "52",  NULL },
         { "53",  NULL },
         { "54",  NULL },
         { "55",  NULL },
         { "56",  NULL },
         { "57",  NULL },
         { "58",  NULL },
         { "59",  NULL },
         { "60",  NULL },
         { "61",  NULL },
         { "62",  NULL },
         { "63",  NULL },
         { NULL, NULL },
      },
      "1"
   },
   { NULL, NULL, NULL, {{0}}, NULL },
};

/* RETRO_LANGUAGE_GERMAN */

/* RETRO_LANGUAGE_ITALIAN */

struct retro_core_option_definition option_defs_it[] = {
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES) || defined(HAVE_VULKAN)
   {
      BEETLE_OPT(renderer),
      "Renderer (necessita riavvio)",
      "Seleziona il renderer video. Il renderer software è il più preciso ma ha requisiti di performance maggiori quando si utilizzano risoluzioni GPU interne più alte.  Il renderer hardware, seppure meno preciso, migliora le prestazioni rispetto al renderer software a risoluzioni interne maggiori e abilita vari miglioramenti grafici. 'Hardware' seleziona automaticamente il renderer Vulkan o OpenGL dipendendo dal driver video front-end di libretro attuale. Se il driver video fornito non è nè Vulkan nè OpenGL 3.3-compatibile allora verrà utilizzato il renderer software.",
      {
         { "hardware", "Hardware" },
         { "software", "Software" },
         { NULL, NULL },
      },
      "hardware"
   },
   {
      BEETLE_OPT(renderer_software_fb),
      "Framebuffer Software",
      "Abilita l'emulazione accurata di effetti framebuffer (es: motion blur, FF7 battle swirl) quando si utilizza renderer hardware usando una copia del renderer software a risoluzione nativa in sottofondo. Se disabilitato, le operazioni precedenti sono tralasciate (OpenGL) oppure renderizzate dalla GPU (Vulkan). Disabilitare questa impostazione può migliorare le performance ma potrebbe causare errori grafici gravi. In caso di incertezza lasciare 'Abilitato'.",
      {
         { "enabled",  NULL },
         { "disabled", NULL },
         { NULL, NULL },
      },
      "enabled"
   },
#endif
   {
      BEETLE_OPT(internal_resolution),
      "Risoluzione GPU Interna",
      "Imposta il moltiplicatore della risoluzione interna. Risoluzioni maggiori di '1x (Nativo)' migliorano la fedeltà dei modelli 3D a spese di requisiti di performance più alti. Elementi 2D sono generalmente non condizionati da questa impostazione.",
      {
         { "1x(native)", "1x (Nativo)" },
         { "2x",         NULL },
         { "4x",         NULL },
         { "8x",         NULL },
         { "16x",        NULL },
         { NULL, NULL },
      },
      "1x(native)"
   },
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
   {
      BEETLE_OPT(depth),
      "Profondità Colori Interni",
      "Imposta la profondità dei colori interni. Una maggiore profondità dei colori può ridurre gli effetti di color banding senza l'uso di dithering. 16 bpp emula hardware originale ma potrebbe mostrare del color banding visibile se il dithering non è abilitato. E' consigliato disabilitare 'Dithering Pattern' quando questa impostazione è impostata su 32 bpp.",
      {
         { "16bpp(native)", "16 bpp (Nativo)" },
         { "32bpp",         "32 bpp" },
         { NULL, NULL },
      },
      "16bpp(native)"
   },
#endif
   {
      BEETLE_OPT(dither_mode),
      "Dithering Pattern",
      "Imposta la configurazione del pattern di dithering. '1x (Nativo)' emula il dithering di risoluzione nativa bassa utilizzato da hardware originale per ammorbidire gli artefatti di color banding visibili a profondità di colore nativi. 'Risoluzione Interna' scala la granularità di dithering alla risoluzione interna configurata per risultati più puliti. E' consigliato di disabilitare questa impostazione quando si utilizza la profondità di colore a 32 bpp. Nota: In Vulkan, abilitando questa opzione forzerà downsampling a profondità di colore native mentre disabilitandola abiliterà automaticamente output a profondità di colore maggiori.",
      {
         { "1x(native)",          "1x (Nativo)" },
         { "internal resolution", "Risoluzione Interna" },
         { "disabled",            NULL },
         { NULL, NULL },
      },
      "1x(native)"
   },
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES) || defined(HAVE_VULKAN)
   {
      BEETLE_OPT(filter),
      "Texture Filtering",
      "Seleziona il metodo di texture filtering. 'Più vicino' emula l'hardware originale. 'Bilineare' e '3-Point' sono filtri di smoothing che riducono il pixellamento tramite sfocamento. 'SABR', 'xBR', e 'JINC2' sono filtri di upscaling che potrebbero migliorare la nitidezza delle texture a costo di requisiti di performance maggiori. Questa impostazione è supportata solo dai renderer hardware.",
      {
         { "nearest",  "Più vicino" },
         { "SABR",     NULL },
         { "xBR",      NULL },
         { "bilinear", "Bilineare" },
         { "3-point",  "3-Point" },
         { "JINC2",    NULL },
         { NULL, NULL },
      },
      "nearest"
   },
#endif
#ifdef HAVE_VULKAN
   {
      BEETLE_OPT(adaptive_smoothing),
      "Smoothing Addativo",
      "Se abilitato, ammorbidisce le immagini 2D e gli elementi UI senza sfocare oggetti renderizzati in 3D. Questa impostazione è supportata solo dal renderer Vulkan.",
      {
         { "enabled",  NULL },
         { "disabled", NULL },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      BEETLE_OPT(super_sampling),
      "Supersampling",
      "Se abilitato, sottocampiona l'immagine dalla risoluzione interna scalata alla risoluzione nativa. Combinando questa impostazione con multiplicatori di risoluzione interne maggiori permette ai giochi di essere visualizzati con oggetti 3D su cui vengono applicati l'antialiasing a risoluzione nativa minore. Produce risultati migliori quando viene applicato ai titoli che utilizzano elementi sia 2D che 3D (es: personaggi 3D su sfondi pre-rendered), e funziona bene in congiunzione con shader CRT. Questa impostazione è supportata solo dal renderer Vulkan. Nota: E' consigliato disabilitare 'Dithering Pattern' se si sta abilitando quest'impostazione.",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(msaa),
      "Multisample Anti-Aliasing (MSAA)",
      "Imposta il livello di MSAA. Migliora l'aspetto degli oggetti 3D. Questa impostazione è supportata solo dal renderer Vulkan.",
      {
         { "1x",  "1x (Predefinito)" },
         { "2x",  NULL },
         { "4x",  NULL },
         { "8x",  NULL },
         { "16x", NULL },
         { NULL, NULL },
      },
      "1x"
   },
   {
      BEETLE_OPT(mdec_yuv),
      "Filtro Chroma MDEC YUV Chroma",
      "Migliora la qualità del playback di FMV (full motion video) riducendo artifatti 'macroblocking' (margini quadrati/frastagliati). Questa impostazione è supportata solo dal renderer Vulkan.",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
#endif
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
   {
      BEETLE_OPT(wireframe),
      "Modalità Wireframe (Debug)",
      "Se abilitato, vengono renderizzati soltanto solo gli spigoli, senza texture o shading, dei modelli 3D. Questa impostazione è supportata solo dal renderer hardware OpenGL. Nota: E' normalmente disabilitato siccome viene utilizzato solamente per scopi di debug.",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(display_vram),
      "Visualizza Full VRAM (Debug)",
      "Se abilitato, viene visualizzata la VRAM dell'intera console emulata. Questa impostazione è supportata solo dal renderer hardware OpenGL. Nota: E' normalmente disabilitato siccome viene utilizzato solamente per scopi di debug.",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
#endif
   {
      BEETLE_OPT(pgxp_mode),
      "Modalità Operazione PGXP",
      "Permette agli oggetti 3D di essere renderizzati con precisione sub-pixel, minimizzando effetti di distorsione e jittering di oggetti 3D visti su hardware originario a causa di uso di punti prefissati per le coordinate dei vertici. 'Solo Memoria', avendo problemi di compatibilità minimi, è consigliato per uso generale. 'Memoria + CPU (Buggy)' può ridurre il jittering ulteriormente ma ha requisiti di performance alti e può causare vari errori geometrici.",
      {
         { "disabled",     NULL },
         { "memory only",  "Solo Memoria" },
         { "memory + CPU", "Memoria + CPU (Buggy)" },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(pgxp_2d_tol),
      "Tolerancia de geometría PGXP 2D",
      "Ocultar errores más evidentes en las operaciones de PGXP: el valor especifica la tolerancia en la que se mantendrán los valores de PGXP en caso de geometrías sin la información de profundidad adecuada",
      {
         { "disabled", NULL },
         { "0px", NULL },
         { "1px", NULL },
         { "2px", NULL },
         { "3px", NULL },
         { "4px", NULL },
         { "5px", NULL },
         { "6px", NULL },
         { "7px", NULL },
         { "8px", NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(pgxp_nclip),
      "Eliminación primitiva de PGXP",
      "Utilice la implementación NCLIP de PGXP. Mejora la apariencia al reducir los agujeros en las geometrías con coordenadas PGXP. Se sabe que hace que algunos juegos se bloqueen en diversas circunstancias.",
      {
         { "disabled", NULL },
         { "enabled", NULL },
         { NULL, NULL },
      },
      "disabled"
   },
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES) || defined(HAVE_VULKAN)
   {
      BEETLE_OPT(pgxp_vertex),
      "Vertice Cache PGXP",
      "Permette alle posizioni dei veritici migliorati da PGXP di essere memorizzati nella cache per riutilizzarli in poligoni. Può potenzialmente migliorare l'allineamento degli oggetti e ridurre cuciture visibili quando si renderizzano texture, potrebbe causare glitch grafici quando vengono trovati falsi positivi mentre si effettua il querying della cache. E' consigliato lasciare quest'opzione disabilitata per il momento. Questa impostazione è supportata solo dai renderer hardware ed è applicata solo quando la 'Modalità Operazione PGXP' è abilitata.",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(pgxp_texture),
      "Prospettiva Texturing Corretta PGXP",
      "Se abilitato, sostituisce il mapping affine nativo di PSX con il mapping prospettiva texturing corretta. Elimina distorzioni dipendenti dalla posizione e warping di texture, risultando in texture correttamente allineate. Questa impostazione è supportata solo dai renderer hardware e viene applicata solo quando 'Modalità Operazione PGXP' è abilitata.",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
#endif
   {
      BEETLE_OPT(display_internal_fps),
      "Visualizza Contatore FPS Interno",
      "Visualizza il contatore del frame rate interno a cui viene renderizzato il contenuto del sistema PlayStation emulato. Nota: Richiede di essere abilitato l'opzione delle notifiche su schermo in libretro.",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(line_render),
      "Line-to-Quad Hack",
      "Seleziona un metodo line-to-quad hack. Alcuni giochi (es: Doom, Hexen, Soul Blade, ecc.) disegnano linee orizzontali allungando triangoli alti un singolo pixel per tutto lo schermo, che vengono rasterizzati come una singola fila di pixel su hardware originali. Questa hack rileva questi piccoli triangoli e li converte a quadrati come necessitato, permettendo a loro di essere visualizzati appropriatamente sui renderer hardware e a una risoluzione interna maggiore. 'Aggressivo' è necessitato per certi titoli (es: Dark Forces, Duke Nukem) ma potrebbe altrimenti introdurre glitch grafichi. Imposta 'Predefinito' se incerti.",
      {
         { "default",    "Predefinito" },
         { "aggressive", "Aggressivo" },
         { "disabled",   NULL },
         { NULL, NULL },
      },
      "default"
   },
   {
      BEETLE_OPT(frame_duping),
      "Frame Duping (Speedup)",
      "Se abilitato e supportato dal front-end libretro, fornisce un minore aumento di performance indirizzando il front-end a ripetere il frame precedente se non risulta nulla di nuovo da visualizzare dal core.",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
#if defined(HAVE_LIGHTREC)
   {
      BEETLE_OPT(cpu_dynarec),
      "Ricompilazione Dinamica CPU (Dynarec)",
      "Ricompila dinamicamente le istruzioni di CPU a istruzioni native. Molto più veloce dell'interpreter, ma i timing della CPU sono meno accurati e potrebbero avere bug.",
      {
         { "disabled", "Disabilitato (Beetle Interpreter)" },
         { "execute",  "Performance Massima" },
         { "execute_one",  "Cycle Timing Check" },
         { "run_interpreter", "Lightrec Interpreter" },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(dynarec_invalidate),
      "Invalidamento Codice Dynarec",
      "Alcuni giochi necessitano di invalidamenti 'Full', alcuni 'Solo DMA'.",
      {
         { "full", "Full" },
         { "dma",  "Solo DMA (Leggermente più Veloce)" },
         { NULL, NULL },
      },
      "full"
   },
   {
      BEETLE_OPT(dynarec_eventcycles),
      "Cicli Eventi Dynarec DMA/GPU",
      "Imposta quanti cicli possono passare nella CPU prima che un aggiornamento GPU o DMA è controllato. Un numero maggiore risulta in velocità maggiori, ma causa anche più probabilmente bug o crash. Questa impostazione ha molto meno impatto su beetle interpreter che su dynarec.",
      {
         { "128", "128 (Predefinito)" },
         { "256",  NULL },
         { "512",  NULL },
         { "1024",  NULL },
         { NULL, NULL },
      },
      "128"
   },
#endif
   {
      BEETLE_OPT(cpu_freq_scale),
      "Frequenza CPU (Overclock)",
      "Abilita overclock (o underclock) della CPU PSX emulata. L'overclock può eliminare rallentamenti e migliorare il frame rate in certi giochi a costo di requisiti di performance maggiori. Alcuni giochi non beneficiano del  overlock avendo un limitatore interno di frame rate. Potrebbe causare certi effetti di essere animati più velocemente del previsto in alcuni titoli quando si esegue l'overclock.",
      {
         { "100%(native)", "100% (Nativo)" },
         { NULL, NULL },
      },
      "100%(native)"
   },
   {
      BEETLE_OPT(gte_overclock),
      "Overclock GTE",
      "Se abilitato, riduce tutte le operazioni emulate GTE (co-processore CPU per grafiche 3D) ad una latenza di un ciclo costante. Per i giochi che fanno un utilizzo pesante di GTE, questa impostazione potrebbe migliorare enormemente il frame rate e la stabilità frame time.",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(gpu_overclock),
      "Overclock Rasterizzatore GPU",
      "Abilita l'overclock per il rasterizzatore 2D contenuto nella GPU del PSX emulato. Non migliora il rendering 3D e ha poco effetto in generale.",
      {
         { "1x(native)", "1x (Nativo)" },
         { "2x",         NULL },
         { "4x",         NULL },
         { "8x",         NULL },
         { "16x",        NULL },
         { "32x",        NULL },
         { NULL, NULL },
      },
      "1x(native)"
   },
   {
      BEETLE_OPT(skip_bios),
      "Salta BIOS",
      "Salta l'animazione di avvio BIOS PlayStation che viene visualizzata normalmente quando si carica del contenuto. Nota: Abilitato questa impostazione causa problemi di compatibilità con numerosi giochi (giochi con protezione da copia PAL, Saga Frontier, ecc.).",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(widescreen_hack),
      "Hack Modalità Widescreen",
      "Se abilitato, renderizza anamorficamente il contenuto 3D e invia in uscita il framebuffer emulato ad un rapporto d'aspetto widescreen. Produce risultati migliori con giochi interamente 3D. Elementi 2D verranno allargati orizzontalmente e potrebbero essere fuori posizione.",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(widescreen_hack_aspect_ratio),
      "Widescreen Mode Hack Aspect Ratio",
      "The aspect ratio that's used by the Widescreen Mode Hack.",
      {
         { "16:10", NULL },
         { "16:9",  NULL },
         { "21:9",  NULL }, // 64:27
         { "32:9",  NULL },
         { NULL,    NULL },
      },
      "16:9"
   },
   {
      BEETLE_OPT(crop_overscan),
      "Taglia Overscan Orizzontale",
      "Da predefinito, i renderer aggiungono un'imbottitura orizzontale (colonne su entrambi i lati dell'immagine) per emulare le stesse barre nere generate in output video analogico da hardware PSX reali. Abilitando questa impostazione rimuove le imbottiture orizzontali.",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      BEETLE_OPT(image_crop),
      "Tagli Addizionali",
      "Se 'Taglia Overscan Orizzontale' è abilitato, quest'opzione riduce ulteriormente la larghezza dell'immagine tagliata di un numero specificato di pixel. Questa impostazione è supportata solo dai renderer software.",
      {
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(image_offset),
      "Offset Immagine Tagliata",
      "Se 'Taglia Overscan Orizzontale' è abilitato, quest'opzione permette all'immagine tagliata risultante di essere slittata orizzontalmente a destra (positivo) o sinistra (negativo) di un numero specificato di pixel. Può essere usato per risolvere problemi di allineamento. Questa impostazione è supportanta solo dai renderer software.",
      {
         { NULL, NULL },
      },
      "disabled"
   },
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES) || defined(HAVE_VULKAN)
   {
      BEETLE_OPT(image_offset_cycles),
      "Offset Immagine Orizzontale (Cicli GPU)",
      "Specifica un numero di cicli GPU da slittare l'immagine. I valori positivi muovono l'immagine a destra, i valori negativi muovono invece l'immagine a sinistra. Questa impostazione è supportata solo dai renderer hardware.",
      {
         { NULL, NULL},
      },
      "0"
   },
#endif
   {
      BEETLE_OPT(initial_scanline),
      "Linea di Scansione Iniziale - NTSC",
      "Seleziona la prima linea di scansione visualizzata quando viene eseguito contenuto NTSC. Impostando un valore maggiore di 0 verrà ridotta l'altezza dell'immagine output tagliando pixel dal margine più in alto. Questa impostazione può essere utilizzata come contromisura per il letterboxing. Necessita riavvio per i renderer software.",
      {
         { NULL, NULL },
      },
      "0"
   },
   {
      BEETLE_OPT(last_scanline),
      "Linea di Scansione Finale - NTSC",
      "Seleziona l'ultima linea di scansione visualizzata quando viene eseguito contenuto NTSC. Impostando un valore minore di 239 verrà ridotta l'altezza dell'immagine output tagliando pixel dal margine più in basso. Questa impostazione può essere utilizzata come contromisura per il letterboxing. Necessita riavvio per i renderer software.",
      {
         { NULL, NULL },
      },
      "239"
   },
   {
      BEETLE_OPT(initial_scanline_pal),
      "Linea di Scansione Iniziale - PAL",
      "Seleziona la prima linea di scansione visualizzata quando viene eseguito contenuto PAL. Impostando un valore maggiore di 0 verrà ridotta l'altezza dell'immagine output tagliando pixel dal margine più in alto. Questa impostazione può essere utilizzata come contromisura per il letterboxing. Necessita riavvio per i renderer software.",
      {
         { NULL, NULL },
      },
      "0"
   },
   {
      BEETLE_OPT(last_scanline_pal),
      "Linea di Scansione Finale - PAL",
      "Seleziona l'ultima linea di scansione visualizzata quando viene eseguito contenuto PAL. Impostando un valore minore di 239 verrà ridotta l'altezza dell'immagine output tagliando pixel dal margine più in basso. Questa impostazione può essere utilizzata come contromisura per il letterboxing. Necessita riavvio per i renderer software.",
      {
         { NULL, NULL },
      },
      "287"
   },
#ifndef EMSCRIPTEN
   {
      BEETLE_OPT(cd_access_method),
      "Metodo Accesso CD (necessita riavvio)",
      "Seleziona il metodo utilizzato per leggere dati dai contenuti delle immagini disco. 'Sincrono' mimica l'hardware originale. 'Asincrono' può ridurre stuttering su dispositivi con storage lenti. 'Pre-Cache' carica l'intero disco immagine sulla memoria quando viene eseguito del contenuto, il che potrebbe migliorare tempi di caricamento in gioco a costo di un ritardo iniziale all'avvio. 'Pre-cache' potrebbe causare problemi su sistemi con poca RAM.",
      {
         { "sync",     "Sincrono" },
         { "async",    "Asincrono" },
         { "precache", "Pre-Cache" },
         { NULL, NULL },
      },
      "sync"
   },
#endif
   {
      BEETLE_OPT(cd_fastload),
      "Velocità Caricamento CD",
      "Seleziona il multiplicatore di velocità all'accesso del disco. Impostando questa opzione a maggiore di '2x (Nativo)' potrebbe ridurre enormemente i tempi di caricamento in gioco, ma potrebbe introdurre dei problemi di timing. Alcuni giochi potrebbero non funzionare appropriatamente se viene impostata sopra un certo valore.",
      {
         { "2x(native)", "2x (Nativo)" },
         { "4x",          NULL },
         { "6x",          NULL },
         { "8x",          NULL },
         { "10x",         NULL },
         { "12x",         NULL },
         { "14x",         NULL },
         { NULL, NULL },
      },
      "2x(native)"
   },
   {
      BEETLE_OPT(use_mednafen_memcard0_method),
      "Metodo Memory Card 0 (necessita riavvio)",
      "Seleziona il formato del salvataggio usato per la memory card 0. 'Libretro' è consigliato. 'Mednafen' può essere usato per compatibilità con la versione stand-alone di Mednafen.",
      {
         { "libretro", "Libretro" },
         { "mednafen", "Mednafen" },
         { NULL, NULL },
      },
      "libretro"
   },
   {
      BEETLE_OPT(enable_memcard1),
      "Abilita Memory Card 1",
      "Seleziona l'abilità di emulare la seconda memory card in slot 1. Se disabilitato i giochi possono solo accedere alla memory card in slot 0. Nota: Certi giochi richiedono questa impostazione di essere disabilitata (es: Codename Tenka).",
      {
         { "enabled",  NULL },
         { "disabled", NULL },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      BEETLE_OPT(shared_memory_cards),
      "Memory Cards Condivise (necessita riavvio)",
      "Se abilitato, tutti i giochi salveranno e caricheranno dagli stessi file memory card. Se disabilitato, file memory card separati saranno generati per ogni oggetto di contenuto caricato. Nota: L'opzione 'Metodo Memory Card 0' deve essere impostato su 'Mednafen' per il funzionamento corretto delle memory card condivise.",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(analog_calibration),
      "Auto-Calibrazione Analogici",
      "Se il dispositivo di input è impostato su DualShock, Controller Analogico, Joystick Analogico o neGcon, quest'opzione permette la calibrazione dinamica degli input analogici. I valori massimi di input registrati sono monitorati in tempo reale e sono usati per scalare le coordinate passate all'emulatore. Questa impostazione dovrebbe venir usata per giochi come Mega Man Legends 2 che richiedono valori maggiori di quelli che i controller moderni provvedono. Per risultati migliori gli stick analogici devono essere roteati alla massima capacità per mettere a punto l'algoritmo della calibrazione ogni volta che il contenuto viene caricato.",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(analog_toggle),
      "Abilita Modalità Toggle Dualshock Analogico",
      "Se il tipo di dispositivo input è DualShock, imposta l'abilità di DualShock emulato di essere cambiato da modalità DIGITALE a ANALOGICO e viceversa come nel hardware originale. Quando quest'opzione è disabilitata, DualShock è impostato su ANALOGICO, se abilitata invece, DualShock può essere alternata tra DIGITALE e ANALOGICO premendo e tenendo premuto START+SELECT+L1+L2+R1+R2.",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(enable_multitap_port1),
      "Porta 1: Abilita Multitap",
      "Abilita/Disabilita la funzionalità multitap sulla porta 1.",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(enable_multitap_port2),
      "Porta 2: Abilita Multitap",
      "Abilita/Disabilita la funzionalità multitap sulla porta 2.",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(gun_input_mode),
      "Modalità Input Pistola",
      "Specifica se utilizzare una 'Pistola Ottica' controllata dal mouse oppure un input 'Touchscreen' quando il tipo di dispositivo è impostato su 'Guncon / G-Con 45' o 'Justifier'.",
      {
         { "lightgun",    "Pistola Ottica" },
         { "touchscreen", "Touchscreen" },
         { NULL, NULL },
      },
      "lightgun"
   },
   {
      BEETLE_OPT(gun_cursor),
      "Cursore Pistola",
      "Seleziona il cursore della pistola che viene visualizzato su schermo quando vengono utilizzati i dispositivi input di tipo 'Guncon / G-Con 45' e 'Justifier'. Se disabilitato, i puntatori (cross hair) sono sempre nascosti.",
      {
         { "cross", "Croce" },
         { "dot",   "Puntino" },
         { "off",   "Disabilitato" },
         { NULL, NULL },
      },
      "cross"
   },
   {
      BEETLE_OPT(mouse_sensitivity),
      "Sensibilità Mouse",
      "Configura la sensibilità dei dispositivi input di tipo 'Mouse'.",
      {
         { NULL, NULL },
      },
      "100%"
   },
   {
      BEETLE_OPT(negcon_response),
      "Sensibilità Torsione negCon",
      "Specifica la sensibilità dello stick RetroPad analogico sinistro quando viene simulato la 'torsione' dei dispositivi input 'negCon' emulati. Lo dislocamento dello stick analogico può essere mappato all'angolo di rotazione del negCon in modo lineare, quadratico o cubico. 'Quadratico' permette una maggiore precisione rispetto a 'Lineare' mentre si eseguono movimenti minori. 'Cubico' incrementa ulteriormente la precisione di movimenti minori, ma 'esagera' movimenti maggiori. Nota: 'Lineare' è consigliato solo quando si utilizzano periferiche volante. I gamepad convenzionali implementano input analogici in una maniera fondamentalmente differente dal meccanismo di 'torsione' del neGcon: la loro mappatura lineare sovramplificano movimenti minori, compromettendo i controlli raffinati. Nella maggior parte dei casi 'Quadratico' provvede l'approssimazione più vicina a hardware reali.",
      {
         { "linear",    "Lineare" },
         { "quadratic", "Quadratico" },
         { "cubic",     "Cubico" },
         { NULL, NULL },
      },
      "linear"
   },
   {
      BEETLE_OPT(negcon_deadzone),
      "Zona Morta Torsione negCon",
      "Imposta la zona morta (deadzone) dello stick RetroPad analogico sinistro quando viene simulato l'azione 'torsione' dei dispositivi input 'negCon' emulati. Utilizzato per eliminare drift dei controller. Nota: La maggior parte dei titoli compatibili con negCon provvedono opzioni in gioco per impostare il valore di zona morta della 'torsione'. Per evitare perdite di precisione, la zona morta in gioco dovrebbe essere quasi sempre impostata su 0. Qualunque aggiustamento necessario dovrebbe essere *solo* impostata tramite questa impostazione ed è particolarmente importante quando 'Sensibilità Torsione negCon' è impostata su 'Quadratico' o 'Cubico'.",
      {
         { NULL, NULL },
      },
      "0%"
   },
   { NULL, NULL, NULL, {{0}}, NULL },
};

/* RETRO_LANGUAGE_DUTCH */

/* RETRO_LANGUAGE_PORTUGUESE_BRAZIL */

/* RETRO_LANGUAGE_PORTUGUESE_PORTUGAL */

/* RETRO_LANGUAGE_RUSSIAN */

/* RETRO_LANGUAGE_KOREAN */

/* RETRO_LANGUAGE_CHINESE_TRADITIONAL */

/* RETRO_LANGUAGE_CHINESE_SIMPLIFIED */

/* RETRO_LANGUAGE_ESPERANTO */

/* RETRO_LANGUAGE_POLISH */

/* RETRO_LANGUAGE_VIETNAMESE */

/* RETRO_LANGUAGE_ARABIC */

/* RETRO_LANGUAGE_GREEK */

/* RETRO_LANGUAGE_TURKISH */

#ifdef __cplusplus
}
#endif

#endif
