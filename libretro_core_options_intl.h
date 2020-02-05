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
      BEETLE_OPT(lineRender),
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
         { "50%",           NULL },
         { "60%",           NULL },
         { "70%",           NULL },
         { "80%",           NULL },
         { "90%",           NULL },
         { "100%(native)", "100% (Nativo)" },
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
         { "1x(native)", "1x (Native)" },
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
         { "disabled", NULL },
         { "1px",     NULL },
         { "2px",     NULL },
         { "3px",     NULL },
         { "4px",     NULL },
         { "5px",     NULL },
         { "6px",     NULL },
         { "7px",     NULL },
         { "8px",     NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(image_offset),
      "Offset Immagine Tagliata",
      "Se 'Taglia Overscan Orizzontale' è abilitato, quest'opzione permette all'immagine tagliata risultante di essere slittata orizzontalmente a destra (positivo) o sinistra (negativo) di un numero specificato di pixel. Può essere usato per risolvere problemi di allineamento. Questa impostazione è supportanta solo dai renderer software.",
      {
         { "disabled", NULL },
         { "-20px",    NULL },
         { "-19px",    NULL },
         { "-18px",    NULL },
         { "-17px",    NULL },
         { "-16px",    NULL },
         { "-15px",    NULL },
         { "-14px",    NULL },
         { "-13px",    NULL },
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
         { "+13px",    NULL },
         { "+14px",    NULL },
         { "+15px",    NULL },
         { "+16px",    NULL },
         { "+17px",    NULL },
         { "+18px",    NULL },
         { "+19px",    NULL },
         { "+20px",    NULL },
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
      "Linea di Scansione Iniziale - NTSC",
      "Seleziona la prima linea di scansione visualizzata quando viene eseguito contenuto NTSC. Impostando un valore maggiore di 0 verrà ridotta l'altezza dell'immagine output tagliando pixel dal margine più in alto. Questa impostazione può essere utilizzata come contromisura per il letterboxing. Necessita riavvio per i renderer software.",
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
      "Linea di Scansione Finale - NTSC",
      "Seleziona l'ultima linea di scansione visualizzata quando viene eseguito contenuto NTSC. Impostando un valore minore di 239 verrà ridotta l'altezza dell'immagine output tagliando pixel dal margine più in basso. Questa impostazione può essere utilizzata come contromisura per il letterboxing. Necessita riavvio per i renderer software.",
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
      "Linea di Scansione Iniziale - PAL",
      "Seleziona la prima linea di scansione visualizzata quando viene eseguito contenuto PAL. Impostando un valore maggiore di 0 verrà ridotta l'altezza dell'immagine output tagliando pixel dal margine più in alto. Questa impostazione può essere utilizzata come contromisura per il letterboxing. Necessita riavvio per i renderer software.",
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
      "Linea di Scansione Finale - PAL",
      "Seleziona l'ultima linea di scansione visualizzata quando viene eseguito contenuto PAL. Impostando un valore minore di 239 verrà ridotta l'altezza dell'immagine output tagliando pixel dal margine più in basso. Questa impostazione può essere utilizzata come contromisura per il letterboxing. Necessita riavvio per i renderer software.",
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
