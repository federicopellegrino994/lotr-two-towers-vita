# Port ARM / PS Vita — stato delle modifiche

Prima tappa verso la Vita: far girare il runtime su un host a 32 bit non x86
(Linux armhf, poi vitasdk). Le modifiche sono attive solo con
`RECOMP_COMPACT_GUEST`: senza, il comportamento per Windows/Linux x86-64 è
identico all'originale (il test `tests/memory_layout_posix` passa uguale).

## Cosa è stato fatto

### 1. Memoria guest compatta (`RECOMP_COMPACT_GUEST`)
Il modello originale riserva tutti i 4 GB del guest più 28 copie speculari
della RAM a indirizzi host fissi: impossibile su un processo a 32 bit.

Ora lo spazio guest è diviso in 256 fette da 16 MB e una tabella dice dove
vive ciascuna:

    host = g_xbox_region_base[va >> 24] + (va & g_xbox_region_mask[va >> 24])

- RAM (64 MB): un solo blocco; le copie speculari fino a 0x80000000 le fa la
  maschera, senza memoria in più.
- Finestra contigua 0x80000000 (64 MB): blocco separato, come l'originale.
- Apertura "tiled" 0xF0000000: alias della finestra contigua via tabella.
- NV2A 0xFD000000, MCPX 0xFE800000, flash 0xFF000000: blocchi propri.
- Fette non mappate: base 0, quindi crash all'indirizzo 0 invece di
  corrompere memoria in silenzio.

Totale ≈ 176 MB di memoria host, nessun indirizzo fisso.

File: `templates/runtime/recomp_types.h` (macro del codice generato),
`src/kernel/xbox_memory_layout.c/.h` (allocazione, tabella, `xbox_HostPtr`),
`CMakeLists.txt` (opzione `XBOXRECOMP_COMPACT_GUEST`, accesa di default su
qualunque CPU non x86 e propagata al gioco).

### 2. Accessi non allineati sicuri
Su ARMv7 `VLDR/VSTR` (float/double) e `LDRD/STRD` (64 bit) vanno in crash
con indirizzi non allineati; il codice Xbox non garantisce l'allineamento.
Su host non x86 ogni `MEMx` passa per una struct packed a un campo: resta un
lvalue (`MEMF(a) = x;` funziona) e il compilatore sceglie da solo la forma
sicura. Verificato sull'assembly ARMv7 (clang, -O2):

| macro      | istruzioni generate            |
|------------|--------------------------------|
| `MEM32`    | `ldr`                          |
| `MEMF`     | `ldr` + `vmov` (niente `vldr`) |
| `MEMD`     | `vst1.8` (tollera disallineo)  |
| `SMEM64`   | due `ldr` (niente `ldrd`)      |

Costo della traduzione indirizzi: due load dalla tabella, un AND, un ADD.

### 3. Registri hardware (MMIO)
APU, AC'97 e USB venivano emulati intercettando i fault e decodificando
l'istruzione x86-64 che li causava: su ARM non può funzionare. In modalità
compatta queste trappole sono spente con un messaggio chiaro nel log.
- Gli accessi APU a indirizzo costante passano già per chiamate dirette
  (`postproc_apu.py` → `xbox_apu_r32/w32`), che funzionano su qualunque host.
- Restano da fare: accessi APU a indirizzo calcolato e USB (OHCI). Per il
  primo avvio: `[Input] usb=0` nell'ini (input HLE) e audio da considerare
  incompleto.

### 4. Strumento di verifica: `tools/compact_audit.py`
Elenca il codice del runtime che fa ancora `offset + va` in modo piatto.
- `WRONG`: indirizzo sicuramente fuori dalla RAM → 0 (i 5 casi trovati con
  0x80000000 fisso in `heroes.c` e `ohci.c` sono stati convertiti a
  `xbox_HostPtr`).
- `CHECK`: 147 punti in cui l'indirizzo arriva da una variabile. Giusti solo
  se è sempre RAM. Il grosso è nel renderer (`nv2a_pb_exec.c`,
  `nv2a_gl.inc`, `nv2a_d3d11.inc`): texture e pushbuffer stanno nella
  finestra contigua, quindi vanno convertiti. Visto che il backend grafico
  va comunque riscritto per la Vita, conviene farlo lì.
- Con `--gen <cartella>` controlla anche il codice generato.

## Test
`tests/compact_guest/` — 18 controlli: specchi della RAM, finestra contigua
separata, alias tiled, aperture NV2A/MCPX/flash, fette non mappate,
coerenza `g_xbox_mem_offset` per la RAM, `MEMF/MEMD/SMEM64/MEM16` a indirizzi
dispari. Tutti passano su x86-64 sia con le macro dirette sia con quelle
packed. Il test originale `memory_layout_posix` passa invariato.

## Prossimi passi
1. Generare il codice dal proprio `default.xbe` (Xbox USA retail):
   `python3 generate.py <cartella del disco>`.
2. Build Linux armhf (Raspberry Pi 2/3) con `XBOXRECOMP_COMPACT_GUEST=ON`:
   prima misura reale delle prestazioni.
3. Convertire i `CHECK` del renderer a `xbox_HostPtr`.
4. Stack del thread principale: il gioco ne usa fino a 64 MB su desktop;
   sulla Vita va creato un thread dedicato con stack grande.
5. Backend grafico per vitaGL (o GXM) al posto di GL 3.3 core.


---

# Stato PS Vita (8 novembre 2026, sera)

**Il gioco gira sulla Vita**: arriva al menu iniziale, la schermata di
caricamento si vede, ~9 fps con il renderer software. Testato su console.

## Come si costruisce
- vitasdk 2026-10-15 (x86_64 linux), più dal sorgente: SDL2 (branch SDL2),
  vitaGL (HAVE_GLSL_SUPPORT=1), vitaShaRK, SceShaccCgExt, math-neon; stub
  taiHEN generati con vita-elf-export dagli exports.yml (NID dai nomi).
  SceShaccCgExt e vitaShaRK vanno compilati con -std=gnu17 (GCC 15).
- `python generate.py default.xbe` (copia Xbox USA retail), poi
  `cmake -S twotowers -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake`
  e `ninja`: produce TwoTowers.vpk.
- Sulla Vita: ux0:data/twotowers/game_files/ = contenuto del disco
  (tools/xiso_extract_xbe.py estrae default.xbe; serve l'estrazione completa).
  Log: ux0:data/twotowers/twotowers_log.txt; crash: ux0:data/*.psp2dmp
  (letti con un parser Python dei core Vita + arm-vita-eabi-addr2line,
  testo a 0x81000000).

## Problemi risolti sulla console (in ordine)
1. emutls di libgcc rotto con pthread-embedded -> vita_emutls.c (stessa ABI).
2. Accessi a regioni non-RAM con la somma piatta -> XR_HOST/XR_PTR inline
   (xbox_memory_layout.h) + tools/compact_rewrite.py su ~80 punti; inverso
   xbox_GuestVa.
3. APU: lettura fisica della finestra contigua tramite tabella.
4. Log una scrittura su card per riga (~12 ms) -> buffer + flush ogni 500 ms.
5. Stack dei thread 64 MB (la Vita li alloca subito) -> 2 MB runtime,
   4 MB thread del gioco, 16 MB thread principale; heap newlib 240 MB.

## Prossimi passi
1. **Renderer GPU con vitaGL** (il collo di bottiglia: ~113 ms su ~117 ms per
   frame sono il rasterizzatore software). Piano: portare nv2a_gl.inc
   mantenendo la trasformazione vertici su CPU (gpu_transform_all) e i pixel
   shader specializzati (hwps_translate) generati in **Cg** invece di GLSL
   330; contesto con vglInit e presentazione con vglSwapBuffers (niente SDL
   video: vitaGL e il renderer GXM di SDL non convivono); UBO -> glUniform.
   Tappe: clear+present, triangoli senza texture, texture, combiner.
   Funzioni GL mancanti in vitaGL: glBlendColor, glDrawBuffer,
   glRenderbufferStorageMultisample, glMaxShaderCompilerThreadsKHR.
2. Audio: l'APU non avanza ("apu 0 periods").
3. Prestazioni CPU: l'emutls costa una chiamata per accesso ai registri
   emulati (RECOMP_TLS); valutare TLS nativa o registri per-thread in un
   blocco puntato da un registro.
4. printf di newlib senza %zu (estetico nei log).
5. 147 punti CHECK residui (tools/compact_audit.py), soprattutto renderer.

## vitaGL: tappa 1 fatta (verificata sulla console, VPK v9)
- fb_present.c (`fb_vgl_loop`, solo __vita__): vglInitExtended(0, 960, 544,
  1 MB, no MSAA), shader Cg compilati sulla console, immagine CPU come
  texture 4:3, vglSwapBuffers. ~24 immagini/s presentate; il gioco resta a
  ~9 fps (119 ms/frame nel rasterizzatore software).
- Lezioni:
  * vglInitExtended restituisce GL_TRUE solo se abbassa la risoluzione: non
    è un flag di successo.
  * Con HAVE_GLSL_SUPPORT=1, glCreateShader(GL_VERTEX_SHADER) tratta il testo
    come GLSL: per sorgenti Cg usare GL_CG_VERTEX_SHADER_EXT /
    GL_CG_FRAGMENT_SHADER_EXT, altrimenti glLinkProgram crasha in SceGxm.
  * vitaGL e il renderer GXM di SDL non convivono: una volta su vitaGL non si
    torna a SDL video.
  * Lo splash di vitaGL resta (scelta di progetto).
  * La libreria video del runtime deve linkare vitaGL & co. (ordine del
    linker): src/video/CMakeLists.txt.
- Tappa 2 (prossima): rasterizzazione su GPU. Punto d'aggancio: il
  rasterizzatore software (nv2a_sw3d.inc) riceve i triangoli già trasformati;
  un backend vitaGL può prenderli lì (stessa trasformazione su CPU) e
  disegnarli in una texture-render-target per superficie, con shader Cg
  generati per configurazione dei combiner (da hwps_translate in
  nv2a_gl.inc). Il presentatore deve allora mostrare la render target invece
  della copia CPU. Attenzione alle letture CPU delle superfici (render to
  texture, screenshot): servirà un readback o il tracciamento GSurf.

## vitaGL: tappa 2 — rasterizzazione su GPU (VPK v10, DA PROVARE SULLA CONSOLE)
Scelta: invece di un backend nuovo agganciato a nv2a_sw3d.inc, il renderer
OpenGL 3.3 esistente (nv2a_gl.inc: superfici GSurf, texture, blit, clear,
combiner specializzati, presentazione) gira su vitaGL attraverso uno strato
di compatibilità, `src/kernel/nv2a_gl_vita.inc`. Vertici sempre trasformati
sulla CPU (gpu_transform_all), come previsto.

- Cosa fa lo strato (dettagli in testa al file):
  * shader in Cg: vertex shader principale, programmi a triangolo pieno
    (copia, blit, clear, presentazione con gamma/saturazione) e un pixel
    shader Cg per ogni configurazione dei combiner (hwps_translate_cg in
    nv2a_gl.inc, stessa logica di hwps_translate). Compilati alla prima
    occorrenza e messi in cache in ux0:data/twotowers/shader_cache/;
  * niente UBO (glUniformBlockBinding in vitaGL è uno stub di 4 byte):
    i dati di gpu_ub() vanno con glUniform4fv a uniform con nome prima di
    ogni draw; le righe intere del blocco combiner diventano float;
  * depth/stencil come renderbuffer GL_DEPTH24_STENCIL8 (vitaGL attacca solo
    texture colore): non campionabili, il primo clear di una zeta aspetta un
    target colore;
  * vertici e indici da array client (vitaGL li copia al draw), indici a
    32 bit; programmi a triangolo con attributo posizione (niente
    gl_VertexID);
  * orientamento delle superfici: autotest all'avvio (disegna in una
    texture e ne legge la memoria). Se vitaGL riempie i framebuffer al
    contrario di OpenGL, y, front face, scissor, viewport e blit vengono
    compensati. Override: [Display] vita_flip=0/1 nell'ini
    (RECOMP_VGL_FLIP);
  * mancano: glBlendColor (fattori costanti -> ONE/ZERO), MSAA, FXAA e
    sharpening (niente derivate), texture BC7 "Ultra", mipmap (spente di
    default: RECOMP_MIPMAPS=1), vertex program su GPU (hwvp off).
- Chi possiede vitaGL (fb_present.c): xbox_VitaGLAcquire/Release. Il renderer
  GPU lo avvia sul thread dell'executor e presenta con vglSwapBuffers; il
  thread dell'immagine aspetta fino a 30 s e, se il renderer non parte o
  fallisce, torna al percorso CPU della tappa 1. Un solo thread chiama GL.
- Default: main.c non forza più renderer=software; l'ini della Vita ha già
  renderer=gpu. `[Display] renderer=software` = comportamento della v9.
- Log da guardare (ux0:data/twotowers/twotowers_log.txt):
  `[VGL] GPU renderer on ...`, `[VGL] surface test: row 0 = ...`,
  `[GL] renderer up`, `[HWPS] ...`, `[VGL] frame N presented`,
  eventuali `[GL] ... did not compile:` (errori Cg) e
  `[VGL] the GPU renderer could not start`.
  RECOMP_VGL_PSLOG=1 scrive i primi shader combiner generati.
- Rischi noti, da verificare sulla console: sintassi Cg accettata da
  SceShaccCg (provata solo a occhio), velocità di vitaGL con molti draw e
  upload BGRA->RGBA sulla CPU, memoria video con molte texture (GTEX_MAX 512).
- Prima prova sulla console (v10): vitaGL parte, gli shader Cg compilano,
  autotest "as OpenGL" (nessun flip). Poi data abort nel thread
  dell'executor: vitaGL copiava una superficie 640 px da 0x05cc4000 --
  nv2a_gl.inc faceva ancora `xbox_GetMemoryOffset() + base` (somma piatta,
  sbagliata con la memoria compatta). v11: i 4 punti convertiti a XR_PTR,
  piu' righe di log scritte subito ([VGL] batch N, [VGL] combiner shader N).
  Lettura dei dump: in THREAD_INFO lo stop reason e' all'offset 0x74
  (0x30004 = data abort); in THREAD_REG_INFO l'ultima parola dopo i
  registri e' l'indirizzo del fault.
- Prossimi passi: provare il VPK; se il quadro è capovolto forzare vita_flip;
  poi profilare (s_3d batches, tempo per frame) e, se serve, vertex program
  su GPU in Cg (hwvp_translate) e meno upload.

### Tappa 2, prove sulla console (9 novembre 2026)
- v10: il renderer GPU parte (`surface test ... as OpenGL`: nessuna
  compensazione), poi crash al primo frame. Dump troncato (.tmp).
- v11: traccia d'avvio `[VGLT] <n> <riga di nv2a_gl.inc> <chiamata>`, scritta
  subito sulla scheda per le prime 1500 chiamate (RECOMP_VGL_TRACE=n, 0 = off).
  Mostrava superfici, zeta, clear e presentazione regolari; il crash era nel
  gioco: `kb_txt_apply` (prompts_kb.inc, chiamato dall'overlay E3 HUD a ogni
  frame presentato) scandiva 0x80010000..0x83FFFF00 con
  `xbox_GetMemoryOffset() + a`, fuori da ogni blocco in modalità compatta.
- v12: `twotowers/src/tt_guest.h` (TT_GP, TT_GBIAS su xbox_HostPtr); tutte le
  macro G8/G16/G32/GF/A8/A32/TT_MEM32 dei file del gioco e le scansioni di
  memoria (heroes.c, menus.c, prompts_kb.inc) ora traducono gli indirizzi
  anche nella finestra 0x80000000. Sul desktop è la stessa somma di prima.
- Leggere un dump: il testo del modulo cambia base a ogni avvio (MODULE_INFO,
  segmento 1 di "twotowers_recomp": 0x81008000, 0x8101d000, ...); indirizzo
  nell'ELF = indirizzo - (base - 0x81000000). Per ogni VPK c'è il suo ELF con
  i simboli (twotowers_recomp-vNN.elf nella cartella). Un dump che finisce in
  .tmp è stato copiato mentre la console lo scriveva: manca la memoria.
- v12: niente crash, ma ~9 s per frame sulla schermata di caricamento. Causa:
  la stessa ricerca dei suggerimenti (kb_txt_apply), ora corretta, scandiva
  tutti i 64 MB della finestra a ogni secondo (nessun testo trovato), sul
  thread di rendering. Più la traccia [VGLT] (una scrittura su scheda per riga).
- v13: ricerca a pezzi (2 MB per chiamata sulla Vita, filtro sul primo
  carattere), traccia spenta di default (RECOMP_VGL_TRACE=n per riaccenderla).
  Riga di versione nel log: `[VGL] GPU renderer (v13) on ...`.
- v13: caricamento a 60 fps, ma non finisce mai. Il gioco gira a vuoto
  (migliaia di Sleep(0) dallo stesso punto, 0F8F3C <- 0E012B <- 1085E8) e
  `apu 0 periods`: l'APU non parte. In modalità compatta i registri APU sono
  memoria semplice; gli accessi a indirizzo costante passavano già per
  xbox_apu_r32/w32 (postproc_apu.py), quelli calcolati di DirectSound
  (this->device->regs + offset) no.
- v14: `twotowers/postproc_apu_io.py` (lanciato da generate.py dopo
  postproc_regs.py): nelle funzioni della sezione DSOUND
  (0x128B60..0x134A34) ogni MEM32 a indirizzo calcolato, fuori dallo stack,
  diventa xio_r32/xio_w32: indirizzi 0xFE800000..0xFE87FFFF all'APU, il resto
  in memoria come prima (2684 accessi). Senza RECOMP_COMPACT_GUEST sono MEM32.
  Da verificare: `apu N periods` > 0 (circa 187 al secondo) e il caricamento
  che finisce.
- v14: `apu 0 periods` invariato; nessun file del disco aperto. Il ciclo a
  0x0E012B è l'attesa normale del frame (thread principale), non il blocco.
  Il blocco vero: DirectSoundCreate resetta i canali AC'97 (RR, bit 1 del
  byte 0xFEC0010B + canale), legge una volta e gira finché RR era alto. Sul
  desktop una trappola in scrittura impedisce a RR di restare
  (ac97_arm_write_trap); in modalità compatta no ("a channel reset will
  spin" nel log). DirectSound non finisce mai, l'APU non parte, il
  caricamento aspetta.
- v15: seconda passata di postproc_apu_io.py: nelle funzioni DSOUND ogni
  scrittura MEM8 a 0xFEC0010B(+canale) passa per XAC97_NORR (toglie RR);
  4 scritture. Da verificare: `[APU] started by the title`, apu periods > 0,
  file del disco aperti ([PATH] ... game_files) e il caricamento che finisce.
- v15: DirectSound parte (`[APU] started by the title`, ~188 periods/s),
  audio ok, si arriva al menu (~25 fps). Premendo Start: crash in vitaGL,
  glClear -> vgl_fast_uniform_copy (nessuna scena GXM attiva: buffer degli
  uniform nullo), dal clear differito della zeta in gpu_bind_targets.
  Ipotesi: memoria di vitaGL esaurita al caricamento del livello (texture
  decodificate in RGBA 32 bit, fino a 512 in cache).
- v16: budget delle texture (RECOMP_VGL_TEXMB, 64 MB) e margine di memoria
  libera (RECOMP_VGL_FREEMB, 12 MB): si liberano le meno usate di recente,
  anche prima di creare una superficie. Log: `[VGL] surface ...` e
  `[VGL] memory (...)` (VRAM/RAM/phycont liberi, texture, quante liberate),
  ogni 300 frame.
- v16: memoria non è la causa (72 MB di VRAM liberi al crash). Crash
  identico: in vitaGL scene_reset chiama sceGxmBeginScene senza controllarne
  l'esito, poi glViewport -> update_scissor_test -> vgl_fast_uniform_copy su
  un buffer di una scena mai iniziata. Avviene subito dopo la creazione di
  un target 256x256 + zeta 256x256 (render-to-texture del livello).
  Ipotesi: framebuffer cancellati (glDeleteFramebuffers distrugge subito il
  render target GXM) mentre scene già accodate li usano ancora. Coerente con
  v10 (crash dopo l'autotest che cancella il suo framebuffer) e v11 (stessa
  build rallentata dalla traccia: nessun crash).
- v17: cancellazioni differite (nv2a_gl_vita.inc, vgl_defer): framebuffer,
  texture e renderbuffer cancellati 3 frame presentati dopo; coda piena ->
  glFinish. Il report memoria conta le cancellazioni.
  Se crasha ancora: provare zeta della stessa dimensione per tutti i target
  o nessuna zeta sui target piccoli.
- v17: crash identico; l'unica cancellazione era quella dell'autotest
  (ipotesi delle cancellazioni scartata, la coda differita resta: innocua).
  Analisi di vitaGL (disassemblato con le righe di debug): glRenderbufferStorage
  registra solo il formato; render target e depth delle FBO nascono pigri in
  scene_reset. Target 256x256 solo colore: clear ok. Stesso target + zeta
  renderbuffer: BeginScene fallisce -> crash. A 640x480 colore+zeta funziona.
- v18: niente depth sui target più piccoli dello schermo (< 640x480), log
  `[VGL] target ... drawn without depth`. Riattivabile per prove:
  `[Display] vita_smallzeta=1` (RECOMP_VGL_SMALLZETA=1). Da capire in vitaGL
  (sorgente: vdeps/vitaGL, framebuffers.c / gxm.c scene_reset) perché la zeta
  piccola fallisce; poi ridare la depth a quei passaggi.
- v18: si entra nel menu, New Game, difficoltà; al primo filmato (mont.bik)
  blocco senza crash. Bink crea due thread decoder: CreateThread fallisce
  (ResumeThread error 8, handle finto 0xBEEF0001), il filmato va a ~8 fps sul
  solo thread principale e alla chiusura (Start) Bink aspetta per sempre i
  due thread mai creati. Causa: vglInitExtended con ram_threshold 1 MB
  lasciava al sistema 1 MB: niente memoria per gli stack dei thread creati
  dopo (sulla Vita lo stack si impegna alla creazione).
- v19: ram_threshold 32 MB (RECOMP_VGL_KEEP_MB); log della memoria libera
  di sistema prima/dopo vitaGL e nel report periodico; un thread guest che
  non si può creare ora lo dice nel log.
  Da fare: Bink su ARM è lento (129 ms di codice del gioco per frame del
  filmato): valutare un decoder nativo o saltare i filmati.
- v19: filmato lento (Bink in x86 ricompilato), saltato con Start; il primo
  livello parte con l'audio. Ma ~2 s per frame: 1650 draw per frame, il
  thread di rendering ~2000 ms, il codice del gioco 372 ms (da solo < 3 fps:
  il limite della CPU con il codice ricompilato, vedi "Prestazioni CPU").
- v20: [VGLPERF] ogni 120 frame: tempo del thread di rendering diviso in
  target, trasformazione vertici, texture, draw call, resto, present e fuori
  dai batch; draw, vertici trasformati, upload di texture per frame.
  1 thread di aiuto per la trasformazione dei vertici (RECOMP_VTX_THREADS).
  Risultato sulla console: ~1400 ms di rendering per frame, di cui la
  trasformazione dei vertici sulla CPU (vertex program interpretati,
  ~7.7 µs per vertice) è la parte più grande; il codice del gioco ~380 ms.
- v21: vertex program sulla GPU. hwvp_translate_cg (nv2a_gl.inc) traduce
  ogni vertex program Xbox in Cg come hwvp_translate fa in GLSL sul PC:
  ingressi vin<i> (array client, location i: vgl_link li lega solo nei
  programmi con un vertex program attaccato), le 192 costanti come uniform
  array `c` (VB_VPC, VB_ROWS 192, caricato solo quando cambia). Se un
  programma non compila si torna alla CPU e il sorgente del primo che fallisce
  va in ux0:data/twotowers/hwvp_fail.txt. [VGLPERF] dice quanti draw passano
  dalla GPU e quanti programmi sono compilati/falliti. RECOMP_HW_VP=0 o
  l'overlay lo spengono. DA PROVARE SULLA CONSOLE.
- v22–v27 (diagnostica): v21 mostrava solo l'HUD (che passa ancora dalla CPU:
  programmi di <= 3 istruzioni). Verificato: la traduzione Cg è giusta
  (simulatore /home/claude/tools/cgsim.py sui dump ux0:data/twotowers/hwvp/),
  gli array di uniform funzionano in vitaGL (test all'avvio [VGL] uniform
  test), un programma vero dà la posizione giusta sulla GPU, e ridisegnati da
  soli in una texture 64x64 ([HWVP] probe, RECOMP_VGL_PROBE=1) tutti i
  programmi del livello coprono i pixel attesi. Trovato anche: la cache degli
  uniform teneva le posizioni di un programma cancellato quando vitaGL ne
  riusa il numero (vprog_forget in vgl_link).
- v28: ipotesi sul nero nel gioco: ogni draw ricaricava tutte le 192 costanti
  (3 KB) e sceGxm le tiene nel vertex ring buffer (2 MB) fino a fine scena:
  1500 draw non ci stanno. Ora ogni programma dichiara solo le righe di c[]
  che legge (tutte solo con indirizzamento relativo) e il ring buffer è 4 MB
  (RECOMP_VGL_VTXRING_MB). DA PROVARE.
- v29–v31: cattura di un frame del livello in .tga (RECOMP_VGL_CAPTURE=n,
  ora spenta di default). Il bersaglio 640x480 resta nero dopo i draw del
  mondo; con il test di profondità spento (cattura m1) il mondo c'è. La
  causa è la profondità, non i vertex program (probabilmente anche la v20 non
  mostrava il mondo). Crash nella cattura m3: si mandavano 192 righe a un
  array c[] dichiarato più piccolo.
- v32: la profondità si azzera disegnando un quadrato a tutto schermo alla
  profondità chiesta (test ALWAYS, colore spento; stencil REPLACE) invece del
  glClear di vitaGL (RECOMP_VGL_ZCLEAR=0 torna al glClear). Le righe di c[]
  di un programma vengono dal sorgente dello shader ("// c_lo"), mai più di
  quelle dichiarate. Risoluzione bloccata a 1x sulla Vita (cambiarla dal menu
  crashava). DA PROVARE.
- v33 (catture sulla profondità): con il confronto ALWAYS, con GEQUAL→LEQUAL
  e con la profondità azzerata subito prima di ogni draw il personaggio c'è.
  Quindi l'azzeramento a inizio frame si perde: vitaGL chiude una scena GXM a
  ogni cambio di bersaglio, e la superficie di profondità non viene salvata
  né ricaricata tra una scena e l'altra (il gioco azzera la zeta su un
  bersaglio colore e disegna il mondo sull'altro: nuova scena, profondità di
  sfondo 1.0, GEQUAL fallisce).
- v34: vgl_depth_keep() forza load/store sulla superficie di profondità di
  ogni framebuffer con zeta (puntatore a active_write_fb+76, come
  vglTexImageDepthBuffer). RECOMP_VGL_ZKEEP=0 lo spegne. Catture spente.
- v34 senza effetto: il puntatore alla superficie di profondità è nullo
  quando si attacca il renderbuffer. Smontando vitaGL: i renderbuffer di
  vitaGL non hanno memoria, ogni framebuffer si crea la SUA profondità al
  primo draw (fb+76: una SceGxmDepthStencilSurface). Una zeta condivisa dal
  gioco tra i due buffer dello schermo qui sono due profondità diverse: il
  clear fatto su uno non arriva all'altro.
- v35: g_zrec (per zeta: ultima profondità/stencil azzerati, con
  generazione) e zseen/sseen per framebuffer; gpu_set_fbo porta il clear
  mancante al framebuffer appena legato (quadrato, zrec_bound). Il
  load/store forzato si fa dopo il primo draw (vgl_depth_keep).
- v35: il mondo si vede (personaggi texturati; alcuni bianchi quando un
  combiner non compila: v36 scrive i falliti in ux0:data/twotowers/hwps_fail/).
- v37: velocità del codice del gioco. Ogni accesso ai registri guest
  (RECOMP_TLS) passava da __emutls_get_address -> pthread_getspecific ->
  pte_osTlsGetValue -> __getreent_for_thread -> sceKernelGetTLSAddr + giro di
  una lista. Ora l'array TLS di ogni thread sta anche negli slot TLS 0x8A/0x8B
  e il percorso veloce è: lettura di TPIDRURO, due load, due confronti
  (vita_emutls.c). Cattura spenta.
- v37: nessun guadagno (codice del gioco sempre ~320–380 ms/frame).
- v38: profiler a campionamento. postproc_prof.py mette RECOMP_PROF_ENTER(va)
  all'inizio di ogni funzione generata (scrive g_prof_fn, rimette quello del
  chiamante all'uscita con una variabile cleanup); twotowers/src/prof_vita.c
  campiona ogni ms e ogni 10 s scrive le 25 funzioni più presenti ([PROF]).
  RECOMP_PROF=0 lo spegne. Il generatore (generate.py) lo esegue per ultimo.
- v41: profilo del solo thread principale nel livello: piatto (le 25
  funzioni in testa ~55%: NtReadFile ~8%, matrici SSE ~4% l'una...). Il costo
  è generale.
- v42: RECOMP_FAST_REGS (solo sorgenti generati): i registri guest (g_eax ...
  g_xmm7, g_mm*, x87) sono campi di una struct per thread (RecompRegs,
  xboxrecomp-0.12.0/src/platform/recomp_fastregs.h) trovata inline da
  TPIDRURO + g_recomp_ctx_off (slot TLS 0x8C/0x8D). Il codice host continua a
  usare le vere variabili thread-local: vita_emutls.c mette la loro memoria
  dentro la stessa struct (xr_ctx_field), quindi vedono gli stessi registri.
- v43–v46 (renderer): getenv per stadio, indice nella cache GL, checksum
  delle texture (lineari una volta per frame, swizzled ogni 16 frame su 1024
  punti, una sola traduzione d'indirizzo), vertici letti dalla GPU
  direttamente dai buffer del gioco (gpu_batch_hw_direct, D3DCOLOR girato
  nello shader con vsw). Frame da ~300 a ~180–280 ms; il collo di bottiglia
  torna il codice del gioco.
- v47 (codice generato): ogni funzione legge la struct dei registri una volta
  sola (RECOMP_CTX_DECL -> _rctx, messo da postproc_prof.py in testa a ogni
  funzione); MEM8/16/32 come puntatori volatile semplici (prima packed
  volatile: accessi byte per byte), MEMF/MEMD/SMEM64 packed non volatile.
