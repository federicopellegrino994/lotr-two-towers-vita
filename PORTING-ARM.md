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
- v49–v51: cattura di un frame della battaglia (al 40° frame con più di
  1000 draw, lista dei draw in cap/draws.txt, schermo ogni 150 draw): il
  terreno (Monte Fato, lava, cielo) viene disegnato, poi due quadrati a tutto
  schermo con blending a colore costante lo coprono: vitaGL non ha il
  blending a colore costante e li disegnava ONE/ZERO (opachi).
- v52: blending a colore costante fatto nello shader dei combiner
  (vgl_blend_const: bsrc/bmisc, righe PSC 72/73). Cattura di nuovo spenta.
- v53: la lista dei draw della battaglia: le ombre (programma 2395CB3B)
  scrivono solo l'alpha (maschera colore 0x01000000, ONE/ONE, MAX), poi due
  quadrati a tutto schermo scuriscono il colore secondo l'alpha. Dopo le
  ombre lo schermo è tutto 0 (anche l'alpha): la maschera colore di vitaGL
  non le ferma. v54: per una scrittura solo-alpha il colore è tenuto anche dal
  blending (RGB ZERO/ONE ADD, alpha con i fattori del gioco).

## v56
- Cattura v55: lo schermo della battaglia diventava tutto 0,0,0,0 fra il draw 600 e 660.
  Causa: vitaGL applica glColorMask solo con il blending attivo. Il quad di clear
  della profondità/stencil (vgl_zclear_quad) e i draw con maschera parziale senza
  blending scrivevano anche il colore. Ora: blending ZERO/ONE per il quad di clear,
  ONE/ZERO (equivalente a nessun blending) per i draw mascherati senza blending.
- Cattura di nuovo disattivata di default (RECOMP_VGL_CAPTURE=n per riattivarla).

## v57
- Il vero colpevole del nero in battaglia: prima delle ombre il gioco fa un CLEAR_SURFACE
  del solo canale alpha (maschera 0x80). glClear di vitaGL ignora glColorMask, quindi
  azzerava anche RGB: scena nera. Ora, con maschera parziale, il clear del colore è un quad
  del colore di clear con blending ONE/ZERO e la maschera applicata (vgl_cclear_quad).

## v58-v60 (diagnostica)
- v57: il terreno in battaglia è tornato (clear del solo alpha).
- Catture v58/v59: la scena 3D su 80130000 è corretta fino a ~1610 draw; anche il front buffer
  80004000 è quasi giusto. Unico difetto vero visto: tre rettangoli blu scuro sopra il fuoco,
  disegnati da B2C7C2B0 (468 indici, blending spento) verso fine frame: probabilmente la
  distorsione del calore che legge una copia del back buffer (gpu_blit) sbagliata.
- Il "freeze" delle catture v58/v59 era la cattura: ogni immagine scritta riga per riga
  (480 sceIoWrite) impiegava 3-4 s. v60: una sola fwrite per immagine.
- v60 salva anche lo schermo reale 960x544 (glReadPixels dopo il present) e la destinazione
  di ogni BLIT, per capire se lo stiramento a destra nasce nel present.

## v62
- Cattura v61 (schermo reale 960x544 + destinazioni dei BLIT):
  1. Lo stiramento a destra: il triangolo a tutto schermo del present (doppio del viewport)
     su GXM non viene tagliato dal viewport; usciva nella banda nera destra ripetendo l'ultima
     colonna (clamp), tagliato dalla diagonale del triangolo. Fix: scissor = viewport nel present
     e nei menu disegnati sullo schermo.
  2. I rettangoli scuri sopra il fuoco: glBlitFramebuffer di vitaGL conta le righe dal basso,
     le superfici tengono la riga 0 in alto: le copie per la distorsione del calore finivano
     specchiate in verticale. Fix: rettangoli di sorgente e destinazione capovolti.
- Cattura di nuovo spenta di default.

## v63
- Log v62: lo shader del combiner AC8BD4C6 non compila (log vuoto, poca memoria), e i draw che
  lo usano (5000-11000 batch per periodo) andavano col fallback, che su Vita restituiva solo il
  colore dei vertici: forme piatte scure = "texture nere che sfarfallano". Il fallback ora fa
  texture dello stadio 0 x diffuse (riga PSC 74 "gfb": scala uv, acceso).
- g_vita_text_pad 16 KB (vita-elf-create: segmenti sovrapposti).

## v64
- AC8BD4C6.txt (hwps_fail): sorgente Cg corretto (alpha test con discard, stadio 1 texture,
  nebbia), log del compilatore vuoto: il compilatore Cg di sistema fallisce senza messaggio,
  quasi certamente per memoria. Ora uno shader fallito viene ricompilato ogni 200 frame
  (max 8 tentativi); appena riesce resta nella cache shader di vitaGL per le partite dopo.

## v65
- Video v57 analizzato a 15 fps: nei "flash" sparisce tutto il mondo per qualche frame, resta l'HUD.
- Bug trovato nel codice: il quad che pulisce la profondità (vgl_zclear_quad) usava il viewport
  rimasto impostato. Quando la pulizia della profondità viene riportata sull'altro colour buffer
  (zrec_bound) subito dopo i target 256x256 delle ombre, puliva solo un angolo 256x256: il resto
  dello schermo restava con la profondità di un frame prima. Ora il viewport è quello del
  framebuffer intero (vgl_zc_viewport).
- Registratore di volo: ultimi 4 frame (draw, clear, clear di profondità con viewport); un frame
  presentato molto più scuro dei precedenti scrive cap/flash_N.txt + .tga (max 3).

## v66 (diagnostica)
- Log v65: i lunghi neri del video sono blocchi di 10-24 s ("1 flips in 24288 ms"): il gioco
  aspetta la GPU, il thread di rendering resta dentro al present (VGLPERF: present 538 ms di media
  su 120 frame). Dopo il blocco il frame è nero (FLASH mean ~1).
- v66: il rapporto [HANG] del watchdog stampa l'ultima chiamata GL del thread di rendering
  (riga di nv2a_gl.inc) e la fase (g_vgl_where: present/swap/compilazione/texture).

## v67
- Log v66: durante i blocchi il thread di rendering era fermo su vglGetTexDataPointer del
  registratore di volo (= la prima cosa che aspetta la GPU): è la GPU che resta occupata 10-20 s.
  Ipotesi: il fragment ring di sceGxm (default 512 KB) tiene le uniform di frammento di tutta la
  scena finché il tiler non la shada; ~1400 draw x ~0.5 KB non ci stanno nelle scene affollate.
- v67: fragment ring 4 MB, parameter buffer 24 MB, VDM ring 512 KB (RECOMP_VGL_FRAGRING_MB,
  RECOMP_VGL_PARAM_MB). Registratore di volo spento (RECOMP_VGL_FLASH=1): la lettura per frame
  aspettava la GPU.

## v68 (velocità)
- Profilo v67 del thread del gioco: 27% in sub_000F8EB3 (= Sleep(0) in attesa del thread di
  rendering: il collo di bottiglia ora è il rendering), 10% in sub_000F95E9 (letture di file).
- Ogni NtReadFile scriveva una riga [READ] + fflush sulla memory card, sul thread del gioco:
  ora solo con RECOMP_READ_LOG.
- Read-ahead (kernel_file.c): letture piccole con offset servite da 3 buffer da 128 KB
  (RECOMP_READAHEAD=0 lo spegne); invalidato a ogni apertura del descrittore e a ogni scrittura.
- gtex_get: una texture già controllata in questo frame non passa più dalla cache di decodifica
  (texc_get_ver, ~10 us a bind, 16-23 ms a frame del thread di rendering).

## v69
- Log v68: frame 110-160 ms (prima 160-190): ~7-9 fps in battaglia. VRAM libera 28 MB, nessuna
  texture scartata: i pannelli neri dell'HUD non sono memoria esaurita.
- Cattura a comando: L + R + SELECT tenuti premuti catturano il frame successivo (immagini,
  draws.txt, schermo). Durante la cattura draws.txt riporta anche il contenuto medio di ogni
  texture decodificata (TEX ... mean rgb / alpha).

## v70
- Cattura v69 (L+R+SELECT) presa proprio su un frame "nero": il mondo e il pannello del termometro
  neri. In draws.txt i draw del terreno (A3A21E8D, texture 80CF3B00 DXT) non hanno nessuna riga TEX:
  texc_get_ver ha restituito NULL -> nessuna texture -> nero. L'unico NULL possibile lì è la malloc
  della copia decodificata fallita: la cache di decodifica CPU teneva una copia ARGB di ogni texture
  (fino a 2048) anche dopo il caricamento sulla GPU: decine di MB, l'heap si esauriva in battaglia.
- Fix: gtex_get libera la copia appena la texture è sulla GPU (texc_drop_last); la voce resta valida
  (checksum, versione); se servono di nuovo i pixel (texture GL scartata, disegno CPU) viene
  ridecodificata.

## v71 (profilo del thread di rendering)
- Utente con v70: "si vede tutto perfettamente", HUD che ogni tanto scompare e riappare.
- Log v70: il thread del gioco passa il 62% del tempo in sub_000F8EB3 (Sleep, in attesa): il collo
  di bottiglia è il thread di rendering, occupato ~100% (120-177 ms a frame: draw 108, metodi
  fuori dai draw 59).
- v71: il campionatore di prof_vita.c campiona anche la "zona" del thread di rendering
  (g_vgl_where, impostata con VWHERE in ogni fase del draw, e il metodo NV2A in corso) e scrive
  [RTPROF] ogni 10 s.

## v72
- [RTPROF] v71 (decodificato: le zone stampate come "method XXXX" sopra 0x2000 sono i 16 bit bassi
  degli indirizzi delle stringhe, base modulo 0x10000 = +0xD000 rispetto all'ELF). In battaglia:
  idle (nessun comando dal gioco) 9-18%, attributi dei vertici 5-18%, glDrawElements 5-13%,
  texture/stage 10-13%, target 4-12%, decodifica texture 1-29% (a ondate), BEGIN_END 5-7%.
  Il thread di rendering aspetta il gioco e il gioco aspetta il rendering: lavorano a turno.
- Affinità dei thread (prima nessuna: con pthread su Vita potevano finire sullo stesso core):
  thread dei frame -> core 0, thread di rendering -> core 1, gli altri thread -> core 0 e 2.
- Impostazioni grafiche bloccate su Vita (gfx_menu.c s_vita_lock): frame rate 30, display,
  aspetto 4:3, risoluzione 1x, anisotropico, AA, vsync, stretch, vertex program GPU, nitidezza,
  ultra texture, cutscene wide, colour grading, HDR, LOD, nitidezza filmati. Luminosità e colore
  restano regolabili.

## v73
- v72 bloccata alla schermata di caricamento: il thread dei frame (lì quello del caricamento)
  fissato sul solo core 0 girava a vuoto in attesa di un altro thread che non veniva eseguito.
  Ora il thread dei frame va sui core 0 e 2 come gli altri; solo il rendering ha il core 1.

## v74 (velocità, entrambi i thread)
- Log v73: frame ~130 ms (era ~160). Il thread del gioco non aspetta più (Sleep 5-12%): ora
  gioco (~115-120 ms) e rendering (~110-125 ms) sono entrambi il collo di bottiglia.
- Codice del gioco (ricompilazione completa): SSE -> NEON su Vita (XMM_MEM/STORE con vld1q/vst1q,
  ADD/SUB/MUL con vaddq/vsubq/vmulq; DIV/MIN/MAX/SQRT restano lane per lane IEEE) in
  recomp_types.h e nel template; -mtune=cortex-a9 per tutto.
- Rendering:
  - percorso diretto anche per i vertici INLINE_ARRAY e per le normali compresse CMP (tipo 6,
    11:11:10, convertite in float solo per quell'ingresso): prima quei draw (i personaggi)
    spacchettavano tutti gli ingressi in float sulla CPU ("vertex attributes" 19% del thread);
  - gpu_stages_vgl: texture e combiner tenuti come al draw prima quando non cambia niente da cui
    dipendono (come gpu_stages di D3D11);
  - gpu_bind_targets: risposta del draw prima riusata quando superficie, clip, formato, zeta,
    run e stato GL non sono cambiati (g_gsurf_gen); getenv per draw tolto.
  - [VGLPERF] stampa stage tenuti/ricostruiti e i motivi dei draw non diretti.

## v75-v76
- v74 bloccata una volta all'avvio dentro il test della superficie (glClear): i test di avvio ora
  solo con RECOMP_VGL_TESTS=1.
- Log v75: il thread di rendering è "idle" 32% del tempo: ora il collo di bottiglia è il thread
  del gioco (~135-160 ms). Il codice generato per l'SSE passa tutto per la pila anche con NEON.
- v76: sub_000FF200 (vec4 x mat4), sub_000FF246 (mat4 x mat4), sub_000FF344 (trasposta) scritte
  in NEON in twotowers_manual.c (~8% del thread del gioco); le versioni generate rinominate _gen
  A MANO in gen/recomp_0019.c (da rifare se si rigenera).
- Display mode mostrato come "Borderless" su Vita (xbox_FramebufferIsFullscreen = 1, bloccato).

## v77
- v74-v76: HUD e menu di pausa spariti (video). I draw spostati dalla v74 erano i ~77 per frame con
  vertici INLINE_ARRAY (HUD, menu): tornano sul percorso CPU (RECOMP_VGL_DIRECT_INLINE=1 per il
  diretto). Lo stage-keeping non tiene più attraverso draw inline, cambi di vertex program
  (s_vp_gen) o di modalità (s_3d.mode).

## v78 (diagnostica HUD)
- Cattura v77 in pausa senza HUD: i 146 draw 2D finali (B2C7C2B0, texture 825Bxxxx 32x32 DXT5) non
  cambiano nemmeno un pixel; i draw dell'HUD visti nella cattura buona (texture 814xxxxx 256x16
  fmt 06) non ci sono proprio. L'HUD, quando sparisce, resta sparito finché ci si trova in quel punto.
- v78: ogni L+R+SELECT in una cartella sua (cap/1, cap/2, ...); in draws.txt stencil, alpha test,
  scissor, i primi vertici dei draw hw e i draw scartati prima del disegno (SKIP); durante la
  cattura le texture vengono decodificate di nuovo (righe TEX con il contenuto medio).

## v79 (HUD e menu di pausa che spariscono)
- Catture v78 (cap/1, cap/2, entrambe senza HUD): i draw dell'HUD (B2C7C2B0, texture 8148A000
  256x16 e 81495000 32x16, posizioni, blend, depth ALWAYS, stencil e scissor giusti) non cambiano
  un solo pixel; i combiner shader sono tutti compilati (0 falliti). Il blit dell'heat haze prima
  dell'HUD c'era anche nella cattura buona della v71: non è quello.
- libvitaGL.a (gxm_utils.o): le uniform di OGNI draw (vertex e fragment) vengono copiate in un
  unico pool circolare fisso da 2 MB (vglReserveUniformCircularPoolBuffer: nessuna attesa della
  GPU, quando è pieno ricomincia da capo). I nostri ~1500 draw a frame mandano ognuno le costanti
  del vertex program (fino a 192 righe = 3 KB con l'indirizzamento relativo, i personaggi) più
  quelle del combiner: nei punti affollati un frame supera i 2 MB, il pool si riavvolge e la GPU
  disegna gli ultimi draw del frame (HUD, menu di pausa) con uniform già sovrascritte. Spiega
  perché dipende dal posto, resta finché si sta lì, e perché dalla v74 (più draw sulla GPU) è peggio.
  Le dimensioni dei ring di sceGxm (vertex/fragment) non c'entrano: vitaGL non li usa per questo.
- v79: link con --wrap=vglSetupUniformCircularPool,--wrap=vglReserveUniformCircularPoolBuffer
  (twotowers/CMakeLists.txt); il pool è nostro (video/fb_present.c), 16 MB (RECOMP_VGL_UNIPOOL_MB),
  dalla memoria di vitaGL. [VGLPERF] uniform pool: KB per frame, frame più pesante, giri del pool.

## v80 (diagnostica: HUD nascosto dal gioco?)
- Log v79: pool delle uniform al massimo 430 KB in un frame (mai vicino ai 2 MB): NON era la causa.
  Pool lasciato nostro ma da 4 MB.
- Utente: quando l'HUD sparisce anche i colpi di spada non vanno a segno -> probabilmente è uno
  stato del gioco (logica), non il renderer: il gioco disegna l'HUD ma trasparente.
- v80: nella cattura, per ogni draw hw, i registri d'ingresso del vertice 0, l'uscita del vertex
  program calcolata sulla CPU (posizione, D0 con l'alpha, T0) e i combiner (icw/ocw, factor0/1,
  final combiner) -- per vedere se l'alpha 0 viene dal gioco.

## v81
- Catture v80 (5: con HUD 1,2,4; senza 3,5): i draw dell'HUD sono IDENTICI in tutto (stato del
  gioco, ingressi, uscita del vertex program con alpha 1, combiner, texture). Non è il gioco a
  nasconderlo. Nei frame senza HUD, da un certo draw in poi (cap 3 ~1420, cap 5 ~1156, in mezzo
  alle particelle AA3EF38B) NESSUN draw cambia più un pixel: haze, HUD, tutto. Il renderer perde
  la coda del frame.
- libvitaGL (misc.o change_cull_mode / glCullFace / glFrontFace): somma front face + cull face
  (+1 se si disegna sul display) e accetta solo 0xD05 / 0xD06; GL_CW + GL_FRONT in un FBO = 0xD04
  -> no_polygons_mode = 1: ogni draw scartato (draw.o) finché lo stato del cull non cambia.
  v81: front face sempre CCW, faccia da scartare scambiata se il front è CW (stesso risultato).
- v81 cattura: per ogni draw lo stato interno di vitaGL (no_polygons_mode, cull, depth, scissor,
  region, framebuffer attivo, render target, programma, viewport, vgl_error).

## v82
- Catture v81 (1 con HUD, 2 e 3 senza): stato interno di vitaGL IDENTICO al draw dell'HUD
  (nopoly 0, cull, depth, scissor, region, framebuffer, programma, viewport). Eppure nelle 2 e 3
  dal draw ~1628 / ~1588 in poi niente cambia più. Non è uno stato GL: è il dato del draw.
- libvitaGL vgl.o: vertici e indici di ogni draw copiati in un pool "circolare" per frame
  (vglSetCircularPoolSize, 32 MB divisi fra i frame in volo); quando un frame lo esaurisce
  (vgl_reserve_data_pool) il resto va in allocazioni di ripiego: è l'unica cosa che cambia "da un
  certo draw in poi" in un frame pesante. v82: pool 48 MB (RECOMP_VGL_DATAPOOL_MB), RAM tenuta da
  parte 24 MB invece di 32 (in battaglia ne restavano 23 liberi) perché venga dalla RAM e non
  dalla VRAM delle texture (29 MB liberi in battaglia). --wrap=vgl_reserve_data_pool: [VGLPERF]
  vertex data pool: KB per frame, frame più pesante, KB disponibili per frame, frame rimasti senza.

## v83 / v84
- v82: pool dati 48 MB + RAM tenuta 24 MB -> vitaGL non parte (niente splash). v83: tutto come v81.
- Log v83: pool dati vitaGL al massimo ~5.3 MB per frame su 10.9 disponibili, 0 frame esauriti:
  NON è la causa della coda del frame persa.
- Utente: nella prima versione (solo nebbia a schermo, la scena non si disegnava) l'HUD era fisso
  -> la perdita dipende dal carico del frame.
- v84: tasto R della Vita = grilletto destro Xbox (prima: tasto White); xinput_device.c.
  Cattura: subito dopo il primo draw dell'HUD due marcatori 8x8 in alto a sinistra (magenta con
  glClear di vitaGL, verde con un nostro quad): nei frame senza HUD dicono se qualcosa arriva
  ancora al target.

## v85
- Catture v84: nella 2 (senza HUD) l'ultimo cambiamento dell'intero frame sono i due marcatori
  (glClear di vitaGL e nostro quad) disegnati subito DOPO il draw dell'HUD, che invece non lascia
  nulla. La GPU/vitaGL non è morta: sono i draw del gioco (vertex program con le sue uniform,
  combiner, depth) a non produrre pixel da un certo punto in poi.
- v85 cattura: per ogni draw clip min/max e la uniform vp (2/w, 2/h, 1/zmax, segno y); dopo il
  primo draw dell'HUD lo stesso draw ripetuto con vp.z = 0 e senza depth test ("REDRAW"): se la
  barra appare in un frame senza HUD, la causa è il clipping in z (clip max) o la profondità.

## v86
- Catture v85 (1 con HUD, 2-4 senza): clip z 0..16777215 e vp.z = 1/16777215 sempre normali; la
  barra ridisegnata con vp.z = 0 e senza depth test NON appare: esclusi clipping in z e profondità.
- Rilettura: "nessun pixel cambia dopo un certo draw" non prova che tutto sia morto -- l'haze
  ricopia la scena su se stessa, le particelle additive scure cambiano poco. Ciò che manca di
  sicuro sono i draw con blending SRC_ALPHA della UI (HUD, menu di pausa): alpha 0 in uscita.
  Il combiner dell'HUD dà alpha = tex.a * diffuse.a, e la diffuse (CPU) è 1 -> sospetta la texture
  sulla GPU (39 upload di texture per frame in battaglia, 2.5 MB/frame: ricambio continuo).
- v86 cattura: dopo il primo draw dell'HUD tre copie della barra più in basso: y+40 senza
  blending, y+80 con una texture bianca al posto della sua, y+120 entrambe; e la texture GL legata.

## v87
- Catture v86: con HUD (2, 3) le copie compaiono (nera senza blending = la texture è davvero nera
  con alpha; bianche con la texture bianca). Senza HUD (1) NESSUNA copia compare, nemmeno quella
  opaca bianca senza blending, mentre i marcatori subito dopo sì -- tutti nello stesso pezzo di
  scena (in cattura glReadPixels = scene_reset + sceGxmFinish ogni 4 draw: niente parameter
  buffer pieno). Il draw con il programma dell'HUD non produce nulla a prescindere da texture,
  alpha, blending e profondità: il programma stesso (vertex/fragment) manca sulla GPU.
- libvitaGL: vglCreateVertexProgram (gxm_utils.o) fa un vertex program per layout degli attributi,
  _glDrawElements_CustomShadersIMPL un fragment program per blend state, con
  sceGxmShaderPatcherCreate*Program, senza controllare l'esito e senza mai rilasciarli.
- v87: --wrap su sceGxmShaderPatcherCreateVertexProgram/FragmentProgram e
  sceGxmSetVertexProgram/FragmentProgram: creati, falliti (primi 10 nel log con il codice),
  programmi NULL impostati; [VGLPERF] shader patcher e nella cattura per ogni draw.

## v88
- Log v87: shader patcher 0 fallimenti, nessun programma NULL: escluso.
- Cattura v87 (senza HUD): dopo le tre BLIT dell'haze 80004000 -> 82BF0000 (glBlitFramebuffer di
  vitaGL) NIENTE arriva più al frame: haze, HUD, copie della barra e anche i marcatori (glClear di
  vitaGL compreso). Rilette così, tutte le catture senza HUD dalla v78 hanno le BLIT subito prima;
  le catture con HUD o non hanno BLIT o le hanno andate bene (quindi il guasto è intermittente).
- v88: su Vita la copia della BLIT è disegnata (il ramo "scale diverse" di gpu_blit: triangolo a
  tutto schermo con viewport = rettangolo di destinazione, shader BLIT), non più glBlitFramebuffer
  (RECOMP_VGL_BLIT=1 per tornare a quello). Da verificare nella cattura (blitdst) l'orientamento.

## v89
- v88 (copia disegnata al posto di glBlitFramebuffer): utente, stesso identico difetto -> la BLIT
  non c'entra; v89 torna a glBlitFramebuffer (RECOMP_VGL_BLIT=0 per la copia disegnata).
- Cattura v88 senza HUD: NESSUNA BLIT nel frame; l'HUD manca, i marcatori (altro programma)
  ci sono. Utente: la "nebbia" sparisce insieme all'HUD. Nebbia/haze e HUD usano lo stesso vertex
  program B2C7C2B0 (passthrough in coordinate schermo): nei frame cattivi falliscono tutti i draw
  con quel programma, gli altri (particelle AA3EF38B, i marcatori) no. Patcher, z, texture,
  alpha, blending esclusi.
- v89 cattura: copie della barra senza blending a y+40 così com'è, y+80 dopo aver rimandato a
  vitaGL tutte le uniform del programma, y+120 anche dopo aver fatto cambiare programma a vitaGL e
  tornare: quale compare dice se manca un'uniform (cache nostra) o lo stato di vitaGL del programma.

## v90 -- CAUSA DELL'HUD CHE SPARISCE (probabile)
- Catture v89 (1, 3, 4 senza HUD): nessuna delle tre copie opache compare, nemmeno con tutte le
  uniform rimandate e il programma cambiato e ripreso; i marcatori sì. Non è lo stato di vitaGL
  né la nostra cache delle uniform: i dati del draw arrivano sbagliati -- e uguali ogni volta.
- vsw (VSC righe 4-7: quali ingressi sono D3DCOLOR da girare B,G,R,A -> zyxw) lo imposta SOLO il
  percorso diretto (gpu_batch_hw_direct); il percorso hw normale (ingressi già float, g_xin) non
  lo azzerava: ereditava quello dell'ultimo draw diretto. In tutte le catture l'ultimo draw diretto
  prima dell'HUD sono le particelle AA3EF38B: quando l'ultima aveva un D3DCOLOR nell'ingresso 0,
  la posizione dell'HUD diventava (z, y, x, w) = x 1 per tutti i vertici: quadrilateri di
  larghezza zero, niente pixel (nessuna "striscia" visibile, confermato). Idem nebbia/haze (stesso
  vertex program, percorso hw), menu di pausa. Dalla v74 (nascita del percorso diretto).
- v90: il percorso hw azzera vsw prima del draw.
- Avvio: la v89 si blocca spesso al caricamento dopo lo splash -- serve il log di un avvio bloccato.

## v91
- v90 (vsw azzerato nel percorso hw): video dell'utente, l'HUD va e viene ancora. vsw non era la
  causa (o non la sola). La correzione resta (è giusta comunque).
- v91 cattura: al primo draw dell'HUD hwvp_probe (lo stesso draw in un target 64x64 riletto: suo
  vertex program + fragment rosso, elements e arrays, poi il suo fragment) -> "PROBE" in
  draws.txt; e i valori delle uniform inviate (vsw, atest, gfb, bsrc, bmisc).

## v92 -- CAUSA DELL'HUD CHE SPARISCE (provata dalla sonda)
- Sonda v91 (5 catture): frame con HUD: elements rosso 48 px, arrays rosso 48 px. Frame senza
  HUD (4 su 4): elements rosso 0 px, arrays rosso 48 px. Uniform (vsw, atest, gfb, bsrc, bmisc)
  identiche. Quindi stesso programma, stessi vertici: fallisce solo glDrawElements con indici a
  32 bit (GL_UNSIGNED_INT da client array), a intermittenza.
- Spiegazione: vitaGL copia i vertici dei client array per glDrawElements solo fino a dove crede
  arrivino gli indici; con indici a 32 bit non li prende tutti e il resto la GPU lo legge da ciò
  che c'era nel pool dati del frame in quel punto (lo stesso draw di un frame prima, se i frame
  combaciano: l'HUD si vede; altrimenti no). Il percorso diretto usa indici a 16 bit e non ha mai
  avuto il problema. Riguardava ogni draw del percorso hw e della CPU: HUD, nebbia/haze, menu.
- v92: vgl_draw_elements converte ogni lista di indici a 32 bit in 16 bit quando il massimo è
  < 65536 (sempre, in pratica).

## v93 -- velocità: traduzione degli indirizzi
- Log pulito v92 in battaglia: thread del gioco 125-160 ms/frame (collo di bottiglia), render
  100-140 ms (17% fermo ad aspettare il gioco). Render: vertex attributes 17%, glDrawElements 15%,
  targets 11%, method 17FC 7%, textures/stages 7%.
- Codice generato (sub_0002CA60, 7400 istruzioni): ~450 accessi in memoria, ognuno
  lsr + ldr base + ldr mask + and + add. Togliere volatile: -0.4% (inutile).
- v93: g_xbox_region_lin[256] (xbox_memory_layout.c, compact_map): host = lin[va>>24] + va,
  esatto per ogni fetta con wrap >= 16 MB (tutte tranne la flash da 1 MB: mappata la sua ultima MB,
  0xFFF00000+). recomp_types.h (gen + template) lo usa salvo RECOMP_COMPACT_MASKED.
  Testo dell'eseguibile da 17.9 MB a 15.2 MB (-15%). g_vita_text_pad 18432 (vita-elf-create).

## v94
- Log v93: nessun guadagno visibile. Gioco 130-145 ms/frame (scena più pesante: 1350-1580 draw
  contro 800-1190 del log v92), render 140-160 ms: adesso il render è alla pari o il collo.
  RTPROF: vertex attributes 14%, glDrawElements 11%, targets 9%, method 17FC 6%, textures 6%.
- v94: cache dello stato degli attributi per vertex array (nv2a_gl_vita.inc: glBindVertexArray,
  glBindBuffer(GL_ARRAY_BUFFER), glEnable/DisableVertexAttribArray, glVertexAttribPointer):
  le chiamate che non cambiano nulla saltate. [VGLPERF] targets per frame: cache dei target
  colpita / mancata (e quante per un nuovo run), run per frame, chiamate attributi e saltate.

## v95
- Log v94: chiamate agli attributi saltate 91% (20402 su 22447 a frame), ma la zona "vertex
  attributes" resta 16.5%: il costo è altrove -- nella stessa zona del percorso hw c'è
  hwvp_pack_range (vertici spacchettati sulla CPU) per gli ~80-95 draw a frame "non diretti:
  attribute type". Target: cache colpita 1186 su 1197, 3.3 run per frame: non è quello.
- Il motivo "attribute type" era quasi sempre un attributo con offset 0 (nessun buffer), che il
  percorso CPU legge come costante (0,0,0,1). v95: nel percorso diretto quell'attributo è una
  costante (0,0,0,1) e il draw resta diretto.

## v96
- Log v95: non diretti ancora 64 a frame (altri tipi di attributo); zone render invariate
  (vertex attributes 17%, glDrawElements 12.5%, "targets" 10% = raster + lookup + scansione indici).
- v96: gpu_set_raster senza i due getenv per draw; vp_decode e hwvp_lookup non ricontrollano il
  programma se s_vp_gen / s_vpd_rev non sono cambiati; i cicli sugli attributi vanno fino
  all'ultimo attributo abilitato (vgl_va_top), non sempre a 16.

## v97 (prova) / v98
- v97 TEST: orologio del gioco (rdtsc, RECOMP_TSC_SCALE) a 0.25. Utente: 7 fps ma molto più
  fluido. Log: il thread del gioco passa il 46% del tempo in sub_000F8EB3 (attesa su un oggetto,
  contata come "codice del gioco"): il suo lavoro vero per frame si dimezza (~65 ms) e poi aspetta
  il render (133-190 ms). Quindi a orologio reale il gioco fa più passi di logica per frame, e con
  il render più veloce il carico del gioco scende da solo: il collo vero è il thread di render.
- v98: orologio reale. I timer VP_T0/VP_ADD e TX_T0/TX_ADD ([VGLPERF]) sono una system call
  ciascuno (sceKernelGetProcessTimeWide), una ventina per draw x ~1500 draw: spenti salvo
  RECOMP_VGL_TIMERS=1 (le righe [VGLPERF] dei tempi danno 0; resta [RTPROF]).

## v99
- Log v98 (timer spenti): render da ~145 a ~100 ms/frame, fermo ad aspettare il gioco il 35% del
  tempo; frame 124-133 ms (8 fps), ora limitato dal gioco (118-126 ms).
- v99: RECOMP_TSC_FRAMECAP_MS (kernel_hal.c xbox_ReadTimeStampCounter): l'orologio del gioco
  avanza al massimo n ms fra un frame mostrato e il successivo. Su Vita 67 (main.c): il gioco non
  simula più di due passi a 30 Hz per frame; sotto i 15 fps va più lento del tempo reale.

## v100 / v101
- Log v99: con il limite a 67 ms il gioco scende solo da ~120 a ~112 ms/frame (ha già un suo
  tetto ai passi); utente: meglio la velocità reale. v100 = v98 (nessun limite).
- v101: codice generato senza RECOMP_PROFILE (twotowers/CMakeLists.txt): niente più
  RECOMP_PROF_ENTER in ogni funzione (testo 15.1 -> 14.5 MB); [PROF] non elenca più le funzioni.

## v102
- Log v101: gioco 103-115 ms/frame in battaglia (era 118-126), 77-98 in scene più leggere; render
  86-140 ms anche con soli 550-750 draw: il costo del render segue i vertici, non i draw
  (vertex attributes 19%, glDrawElements 17%).
- v102: i vertici dei draw diretti in vertex buffer object (vbs_place: l'intervallo di vertici
  copiato una volta sola, per gruppo interleaved, in uno di 3 buffer da 3 MB a rotazione per
  frame; vitaGL disegna dal buffer senza copiare ogni attributo). CMP e inline restano client
  array. RECOMP_VGL_VBO_MB=0 lo spegne. [VGLPERF] vertex buffers per frame.

## v103
- v102 (vertex buffer object): video dell'utente, geometria a pezzi e 3 fps. Spento di default
  (RECOMP_VGL_VBO_MB=0); v103 = v101 nel comportamento.

## v104 -- i quad singoli raggruppati
- Catture: ~700 dei ~1500 draw di un frame di battaglia sono un quad solo ciascuno (AA3EF38B e
  9D5CFD70 additivi: scintille/braci; 4E42FF1C: decal), ognuno col costo pieno di un draw.
- v104 (gpu_batch_impl, vsm_*): i draw fino a 6 indici con vertex program passano dal percorso CPU
  (vertex program interpretato sui 4 vertici) e quelli consecutivi con lo stesso stato (VsmKey:
  target, stage, blending, depth, alpha test, fog, combiner) si accumulano in un solo
  glDrawElements. Lo stato GL e le uniform si impostano all'inizio del gruppo; il gruppo esce
  prima di ogni altro draw, clear, blit, present. RECOMP_VGL_QUADS=0 lo spegne.
  [VGLPERF] single quads per frame: N, drawn in M runs.

## v105 -- modalità leggera
- Log v104: 633 quad in 323 gruppi, ma il percorso CPU costa più di quanto risparmia (render
  150-180 ms). Raggruppamento spento (RECOMP_VGL_QUADS=1 per riaverlo).
- v105 modalità leggera (RECOMP_VGL_LITE=0 la spegne), su richiesta dell'utente: non si disegnano
  i quad singoli con blending (scintille, braci, fumo, sangue, decal), i draw nei target più
  piccoli di 640 (ombre dei personaggi), i draw che campionano un render target o la destinazione
  di una BLIT (ombre sul terreno, heat haze), e le BLIT tra superfici. [VGLPERF] light mode.

## v106
- Log v105 (modalità leggera): 360-710 quad saltati a frame, render fermo il 39% del tempo, ma il
  gioco 130-160 ms/frame: 7-8 fps come prima. Quadrati neri sulla mappa (i decal/ombre saltati).
  Le regole su ombre e render target non scattavano (0). Modalità leggera spenta di default
  (RECOMP_VGL_LITE=1 per riaverla). Conclusione misurata: il limite è il thread del gioco.

## v107 -- il codice del gioco più veloce
- Il profilo del thread del gioco è piatto (nessuna funzione oltre il 7%), quindi due interventi:
  1. `twotowers/postproc_fast.py` (passo sul codice generato, idempotente):
     - accessi "K": stack e indirizzi costanti stanno sempre nella RAM guest, che è un solo blocco
       host: `KMEM32(L_esp + 8)` = `_k0 + va` invece di `tabella[va >> 24] + va` (310104 accessi in
       8304 funzioni). `ebp` conta come stack solo nelle funzioni che lo usano come frame pointer.
     - registri SSE come variabili locali di tipo vettore NEON (221 funzioni), riscritti nei globali
       attorno alle chiamate come fa postproc_regs.py con quelli interi.
     - recomp_0002.c: 446 KB -> 385 KB di codice; sub_0002CA60 da 7646 a 4570 istruzioni.
     - test differenziale (x86-64, stato casuale): 460 funzioni confrontate, 0 differenze.
  2. `twotowers/src/native_hot.c`: scritte a mano il test linea-triangolo (0x3FD30, uscite rapide) e
     i sei passi dell'ombra dei personaggi (0x2FA10, 0x2FAC0, 0x2FCA0, 0x2FD30, 0x2FE40, 0x2FED0).
     Identiche alle originali su 4000 test linea e 500 mesh x 6 passi. I corpi originali restano come
     `sub_X_gen`; TT_NATIVE_HOT=0 li rimette.
- [PERFG] nel log: scritture al chip audio (e attese sul lock del thread audio), letture file,
  chiamate delle funzioni native.
- Copia del codice generato prima del passo: /home/claude/work/gen.backup_v106.

## v108 -- misure del thread di disegno
- Log v107: codice del gioco 134 -> 107 ms per frame (v101 -> v107, stessa grafica); il thread di
  disegno resta a ~137 ms e ora è lui il limite (il gioco lo aspetta ~29 ms a frame).
- Il campionatore [RTPROF] conta il tempo reale (anche quando il thread è sospeso dal sistema):
  aggiunta la riga [CPU] con il tempo di processore vero di render, gioco e audio
  (sceKernelGetThreadInfo runClocks) e il costo delle creazioni di fragment program (~1000 ogni
  dieci secondi, vitaGL ne crea uno nuovo a ogni cambio di blending per programma).
- Zone più fini nel percorso diretto: index range, triangle list, swizzle constants, raster state,
  vertex program lookup, after the draw.

## v109 -- costanti dei vertici solo dove cambiano
- Log v108, riga [CPU]: il thread di disegno lavora davvero (gira il 97% del tempo, non è il sistema
  a fermarlo); i fragment program di vitaGL costano 0,06 ms a frame (trascurabili). Il thread audio
  usa 60-110 ms di CPU a frame, sui core 0 e 2 insieme al gioco.
- La zona "direct: swizzle constants" (11-12%) era in realtà il confronto e le due copie di tutte le
  192 costanti (3 KB) a ogni draw (il compilatore aveva spostato l'etichetta). Ora il pushbuffer
  segna le righe cambiate (s_vpc_lo..hi, VPC_MARK) e al draw si copiano solo quelle in g_vub.
  RECOMP_VGL_VPC_ROWS=0: il vecchio modo.
- env.txt in ux0:data/twotowers: NOME=VALORE per riga, impostati all'avvio (per i test, es.
  TT_NATIVE_HOT=0 rimette le funzioni originali al posto di quelle riscritte a mano).

## v110
- Log v109 (env.txt con TT_NATIVE_HOT=0): thread di disegno 137 -> 122 ms per frame grazie alle
  costanti per righe; gioco 116 ms (107 con le funzioni native). L'utente: i colpi vanno a segno
  meglio con le funzioni originali. Il test linea-triangolo nativo (0x3FD30) ricontrollato riga per
  riga contro il codice lifted (calcoli in float/double, confronti, registri d'uscita): nessuna
  differenza trovata; comunque spento di default (TT_NATIVE_RAY=1 per riaverlo). Le sei funzioni
  dell'ombra restano native (disegnano soltanto).
- Audio: il thread del suono tiene il lock dell'APU da un frame di 32 campioni all'altro (e li fa di
  seguito quando è indietro): una scrittura del gioco aspettava in media ~3 ms, 10-20 ms a frame.
  Ora, se una scrittura aspetta (g_apu_waiters), il thread del suono lascia il lock dopo il frame
  in corso. RECOMP_APU_HANDOFF=0: come prima.

## v111
- Log v110 (fino al secondo livello): attese sul lock audio da 10-20 ms a ~0,4 ms per frame; gioco
  ~65-100 ms, disegno ~105-128 ms: ora il limite è il thread di disegno.
- [RTPROF] era sbagliato sulla Vita: le stringhe delle zone stanno sopra 0x80000000 e il
  campionatore le prendeva per metodi ("method 12A4"); i nomi ricavati con rtmap.py erano
  indovinati. Ora i metodi hanno chiavi piccole (0x1xxxx / 0x2xxxx) e le zone escono per nome.
- fhash_note (la lista delle texture del frame) con una piccola hash invece della scansione.

## v112 -- il blocco dopo i filmati
- Log v111: al terzo livello (filmato gat1_1) PsCreateSystemThreadEx non riesce a creare i due
  thread del Bink ("could NOT be made (error 8)") e il gioco li aspetta per sempre. Causa: i thread
  di CreateThread (win32_compat.c) erano joinable e nessuno faceva join: finiti, tenevano il loro
  stack (4 MB per worker del guest). Due per filmato: dopo 4-5 filmati la memoria per gli stack era
  finita. Ora sono detached (TerminateThread non cancella più un thread già finito).
- [RTPROF] con i nomi veri (v111): glDrawElements di vitaGL 19,7%, texture lookup/decode 9,8%,
  textures/stages 9,1%, vertex attributes 8,7%, metodo 17FC (BEGIN_END prima del batch) 8,0%,
  program 4,9%. Zone più fini: tex: decode cache / GL texture slot / upload / after / checksum, e
  sw3d: batch start / vertex program decode / stages and combiners / attributes needed,
  draw_primitive.

## v113 -- misure per i filmati e per le texture
- v112: il terzo livello parte (thread detached). Profilo del disegno: tex: decode cache 7,5%,
  tex: GL texture slot 3,1% (la ricerca delle texture, quasi mai un checksum: 0,2%).
- Profilatore [PROF] acceso solo su recomp_0032.c / recomp_0033.c (0x137396-..., le sezioni BINK*),
  su tutti i thread: dove la decodifica dei filmati passa il tempo.
- [VGLPERF] texture lookups: trovate di nuovo nel frame, cercate, spinte fuori da un'altra,
  scansioni complete della cache di decodifica e degli slot GL.

## v114 -- i filmati si vedono
- Il video dell'utente: al terzo livello, al posto del filmato, l'anello di fuoco della schermata
  di caricamento. gat1_1.bik (decodificato qui con ffmpeg dai file del gioco) sono 17 s di film.
  Il gioco decodifica i fotogrammi Bink in due texture YUY2 640x480 lineari (0x82DB2000 /
  0x82D1C000) e le disegna con un quad. s_texc_frame si muoveva solo in nv2a_d3d11.inc: nel
  renderer GL restava 0, ogni texture aveva la stessa epoca per sempre e una texture riscritta dalla
  CPU veniva letta una volta sola. Ora le texture lineari si ricontrollano una volta per frame
  mostrato (s_texc_vframe). RECOMP_TEX_LINEAR_ONCE=1: come prima.
- [PROF] sul codice Bink (gat1_1): sub_0014A780 23%, sub_0014A010 22% (IDCT, colonne: costante
  0xB50 = 2896), sub_00145050 15%, sub_00149010 7,7%, sub_00148060 6,9%, sub_0013E200 5,9% (MMX).
  Un fotogramma ~65 ms (15 al secondo); il filmato è a 29,97.
- Comandi: L della Vita = grilletto sinistro (arco); levetta destra in basso = la vecchia L (il
  passo indietro, LB).

## v115 -- niente ombre dei personaggi
- sub_00030360 costruisce e disegna l'ombra (shadow volume) di un personaggio: le sei fasi
  0x2FA10..0x2FED0 e le passate di stencil. Ha già un'uscita per un personaggio senza ombra
  (obj+0x5F8 == 0 -> loc_00030518, che chiude come sempre). recomp_0003.c ora la prende anche
  quando tt_shadows_off() (native_hot.c): ombre spente di default, TT_SHADOWS=1 le rimette.

## v116 -- misure: draw unibili, compilazioni
- Log v115 (ombre spente, filmati giusti): il tempo di un frame segue il numero di draw. 500-600
  draw: 55 ms (18 fps); 900: 85 ms; 1400: 117 ms; 1900-2000: 165 ms. ~70 us di thread di disegno
  per draw; il gioco ora 30-73 ms, sempre sotto il disegno. Il thread di disegno lavora il 75% del
  suo tempo, il resto è fermo dentro vitaGL (attesa della GPU?). glDrawElements di vitaGL 37% delle
  zone. Picchi di 0,7-3,6 s all'inizio dei livelli (superfici nuove, ritratto E3HUD, shader).
- [VGLPERF] joinable draws: draw senza alcun cambiamento dal precedente (e sugli stessi vertex
  buffer), solo costanti cambiate, draw da 2 triangoli o meno. Tempo delle compilazioni shader.

## v117 -- shader su scheda, draw unite, effetti opzionali
- Log v116: ogni shader compilato a runtime costa 250-475 ms (fino a 35 in dieci secondi: 16 s) e
  si ricompilava a ogni avvio: la cache di vitaGL (vglSetShaderCachePath) vale solo per i suoi
  shader a funzione fissa. Ora gpu_shader_start legge shader_cache/s_<hash sorgente>.bin con
  glShaderBinary; dopo il link di una coppia, glGetProgramBinary dà i due shader serializzati
  ([4][128][u32 n][vs][u32 n][fs], ognuno: count, indici dei parametri, GXP) e li si scrive.
  RECOMP_VGL_SHC=0: spento. Se la grafica è rotta: cancellare ux0:data/twotowers/shader_cache.
- Draw unite: il 20% delle draw non cambia nulla dalla precedente (s_last_chg == 0) e legge gli
  stessi vertex buffer. Una draw diretta resta in sospeso (stato già impostato, lista in g_didx) e
  quelle così che la seguono aggiungono solo i triangoli; jn_flush prima di ogni altra cosa che
  tocca GL (altra draw, clear, blit, present, readback, inizio/fine di un kick).
  RECOMP_VGL_JOIN=0: spento.
- TT_EFFECTS=0 (env.txt): non disegna i quad singoli additivi (scintille, braci, bagliori).
- Metà delle draw di una scena pesante (1096 su 2067) sono di 2 triangoli o meno.

## v118 -- il crash della cache shader
- v117 al secondo avvio: data abort in _malloc_r (heap rovinato) alla prima draw con shader letti
  dalla scheda (core dump: pc ELF 0x81B9C6BC, da glDrawElements). Causa: lo shader serializzato di
  vitaGL non contiene la lista dei blocchi uniform (shader+816: nodi name[128], indice buffer,
  next) che glCompileShader costruisce; i nostri shader ne hanno tutti. Ora il file (t_<hash>.bin,
  magic "TTS2") tiene anche quella lista, rimessa dopo glShaderBinary. La tabella degli shader di
  vitaGL non è esportata: sta 0x67004 dopo is_shark_online (controllo a ogni uso: tipo giusto).
- Draw unite (log v117): 170-410 draw unite per frame; scene da ~2000 draw: 139 ms (erano 165-198).

## v119
- v118 crashava ancora al secondo avvio (heap rovinato, _malloc_r da hwvp_translate_cg). Trovato
  nel nostro strato: vgl_shader_source ricava dal sorgente i nomi degli input (vin0.. invece di
  a_p..) e l'intervallo delle costanti c[] dello shader (g_vgl_vin_sh, g_vgl_sh_clo/cn); uno shader
  letto dalla scheda non passa da glShaderSource e il suo programma veniva collegato con i nomi
  sbagliati e senza intervallo. Ora vgl_shader_note_src viene chiamata anche per quelli.
  Non verificato sulla Vita che fosse l'unica causa. RECOMP_VGL_SHC=0 spegne la cache.

## v120 -- dove il thread di disegno sta fermo
- v119: la cache shader funziona (84 letti dalla scheda, 0 compilazioni, niente cali bruschi).
- Log v119 con TT_EFFECTS=0: solo 4-27 quad saltati a frame (il filtro "additivo" prende poco):
  nessun guadagno. Scene da ~2100 draw: frame 145-179 ms, il gioco aspetta il disegno 57-101 ms a
  frame; il thread di disegno è occupato ~142 ms ma gira ~104: ~40 ms a frame è bloccato (GPU o
  anelli di GXM pieni?). Thread audio: 80-98 ms di CPU a frame (su core 0/2 col gioco).
- La riga [CPU] dava per il "gioco" il tempo del thread di disegno (il report gira lì): ora usa
  g_thid_game.
- [VGLBLK]: tempo reale e tempo non in esecuzione attorno a draw (1 su 8), cambi di target, clear,
  copie, present.

## v121
- Log v120 ([VGLBLK]): nessun punto dove il thread di disegno resti bloccato a lungo. Su ~1900 draw:
  le draw ~60 ms reali (di cui ~13 non in esecuzione), il resto (~90 ms) è il nostro lavoro per
  draw, distribuito (profilo piatto). La misura stessa costava ~35 ms a frame (due syscall, ~12 us):
  ora solo con RECOMP_VGL_TIMERS=1. Conclusione: non è la GPU, è CPU per draw (~75 us), 40% in
  vitaGL e 60% nostro. Leva rimasta: meno draw.
- TT_EFFECTS=0 ora salta tutti i quad singoli disegnati con blending (erano 358-710 a frame nella
  v105); TT_EFFECTS=2 solo gli additivi (come v117).

## v122 -- effetti di default, vertici letti sul posto (di prova)
- Log v121 con TT_EFFECTS=0: 120-690 quad saltati a frame; scene pesanti (~1750 draw del gioco)
  100 ms a frame (erano 145-190), mediana 68 ms (~15 fps). Ora glDrawElements di vitaGL è il 51%
  del thread di disegno e non scende con il numero di draw: è la copia dei vertici (per ogni
  attributo copia stride x vertici: un vertice interlacciato a 5 attributi, 5 volte).
- TT_EFFECTS di default = 3: saltati i quad singoli con blending tranne quelli additivi (bagliori;
  l'utente vuole tenere il mirino dell'arco e gli effetti delle combo: da verificare che siano
  additivi). 1 = tutto disegnato, 0 = nessuno, 2 = saltati solo gli additivi.
- RECOMP_VGL_INPLACE=1 (di prova, spento): gli attributi delle draw dirette con 24+ vertici
  puntano direttamente al vertex buffer del guest: glVertexAttribPointer con vertex_array_unit
  (globale di vitaGL) = un nostro "buffer object" con memoria 0, così vitaGL non copia e passa
  l'indirizzo a sceGxmSetVertexStream; la fetta da 16 MB del guest viene mappata per la GPU con
  sceGxmMapMemory (ip_slice). Rischio: la GPU legge a fine scena, i buffer riscritti a ogni frame
  possono dare geometria sbagliata (per questo solo 24+ vertici).

## v123 -- audio più leggero, finestra contigua in un blocco suo
- Log v122, quarto livello (area piccola, tanti nemici): qui il limite torna il thread del gioco
  (86-106 ms a frame, attesa sul disegno 6-11 ms) e il thread audio usa 78-95 ms di CPU a frame
  sugli stessi due core (0 e 2): i due insieme li saturano (e l'audio gratta).
- Audio: attenuate() era un powf a ogni chiamata (8+ per voce per frame, 1500 frame al secondo):
  ora tabella da 4096 valori (identici); il powf del pitch e del filtro solo quando cambiano; i bin
  con guadagno zero non vengono sommati.
- RECOMP_VGL_INPLACE=1 nella v122: sceGxmMapMemory rifiutava un pezzo dell'heap C (805B0017). Ora
  la finestra contigua (64 MB, 0x80000000) è un blocco di memoria suo (sceKernelAllocMemBlock
  USER_RW; heap newlib 240 -> 176 MB) e viene mappata intera alla prima draw in-place.

## v124 -- la stessa texture sotto due chiavi
- Log v123 (quarto livello, folla): audio gratta molto meno. Il thread di disegno: tex: GL texture
  slot 15,8%, decode cache 8,3%, checksum 5,5% -- ~450 ricerche a frame "spinte fuori da un'altra",
  ognuna una scansione di tutti gli slot. La cattura mostra perché: le stesse texture swizzled
  (es. 80BB0000 64x64 fmt 19) legate con pitch 0 e 1024 da una draw all'altra; il pitch entrava
  nella chiave delle cache (decode e GL): due voci per la stessa texture che si scacciano. Ora il
  pitch di una texture swizzled/compressa è 0 (nv2a_sw3d.inc, setup degli stage).
- RECOMP_VGL_INPLACE: nella v123 geometria sbagliata e gioco bloccato (la GPU legge a fine scena
  buffer già riscritti). Spento per sempre (solo =2 per test).

## v125 -- tagli dalla cattura del quarto livello
- Cattura (folla, 810 draw): 192 draw (24%) dipingono le ombre tonde dei personaggi (2 quad a
  testa, texture 32x32, vertex program AA3EF38B) in un target 256x256 (80A58000) steso sul terreno;
  164 draw (20%) sono il passaggio delle luci (vertex program 453E894B, blend ONE/ONE: lo scenario
  ridisegnato additivo con la texture di attenuazione 80BB0000); 396 lo scenario (1A782B41).
- Spenti di default: TT_BLOBS=1 riaccende le ombre tonde (quad con blending in un target 256x256),
  TT_LIGHTS=1 il passaggio delle luci (453E894B + ONE/ONE, nel percorso diretto).
- (anche v124: pitch 0 per le texture swizzled/compresse; INPLACE spento)

## v127
- Profilo v126 (folla, quarto livello; il gioco è il limite: 56-104 ms a frame). Piatto: in testa
  sub_0012C3AD 5,7% + sub_0012C3E0 1,3% (il lock del gioco attorno ai suoi oggetti: alza l'IRQL a
  DISPATCH con KfRaiseIrql/KfLowerIrql, ordinali 160/161), poi 0x3FD30 4,7% (test linea-triangolo),
  2D5C0 4,3%, 11A830 3,8%, 38400 3,7%, 2CA60 3,5%, 37F10 3,4%.
- irql_hist (kernel_hal.c) a ogni raise/lower leggeva l'orologio e scorreva fino a 96 parole dello
  stack guest (solo per il dump [IRQLBUG]): ora solo con RECOMP_IRQL_HIST=1.
- Profilatore di nuovo spento.

## v128 — the game's waits for the GPU
- The spike frames (game code 130-165 ms with the render thread at ~50 ms) were spent in
  0x0011AC90 (BlockUntilNotBusy) -> 0x0011A8E0 (BlockOnTime) for the *latest* time: a
  resource still set on the device was locked. D3D then inserted the time, kicked, and slept
  on the GPU-idle event (NOP 5), which here is set only by the kernel's clock thread after it
  takes the dispatch lock. That path did not count as waiting in [PERF].
- Now (twotowers_manual.c) that case calls 0x0011A830 (the time in + the kick) and waits
  natively for the render thread's semaphore release; the release wakes the waiter at once
  (xbox_PbKickWaiter). TT_BOT_NATIVE=0 restores D3D's own path.
- New log lines: [BOT] (waits per 5 s, full drains, ms in them) and a second [CPU] line
  (timer thread CPU, ticks, time waiting for the dispatch lock).

## v128 result / v129
- v128 log: only 15 full drains in 194 s (380 ms) -- the native drain touched a rare case; no fps change.
  Gameplay [RTPROF]: the render thread is idle 55-70% ("no commands from the game") -- the game thread
  is the limit (crowds: game code ~71 ms, render ~57 ms). Lighting is vertex-shader work on the GPU
  (constants via 0x00116A40 = one vec4, method 0x1EA4/0x0B80) and the lights pass is already skipped,
  so cutting lighting does not touch the bottleneck.
- Timer thread: 10-30 ms per frame waiting for the dispatch lock in heavy scenes (not the fps limit).
- v129: RECOMP_PROFILE on for all generated code, game thread only (0032/0033 back to recomp_tp_() == g_prof_tp).

## v129 profile (game thread only) / v130
- Crowded windows (game code 85-94 ms): about 22% of the game thread in 0x12xxxx --
  DirectSound (game_audio) and its locks: 0x0012C3AD / 0x0012C3E0 (raise/lower IRQL, 7-9%),
  0x00128C07 (DSound's critical section, 2-4%), 0x12E505, 0x12D2B9, 0x12E5C6, 0x1293D9, 0x12DB00.
  The rest is flat game code: 2D5C0, 2CA60, 37F10, 38400, 3FD30, 471F0, F79C0, 11A830 (3-7% each).
- The timer thread waits 10-30 ms per frame for the dispatch lock in the same scenes.
- v130: xbox_CpuLock on the Vita spins 200 tries, then blocks in pthread_mutex_timedlock (2 s)
  instead of Sleep(0)/Sleep(1) polling (RECOMP_CPU_LOCK_POLL=1: the old way). [CPU] now also
  prints the lock's takings per frame, how many found it held, and the time waited.

## v130 result / translator experiment / v131
- v130 (blocking dispatch lock): 12C3AD fell from 7-9% to ~3% of the game thread, frame times unchanged
  (64/77 ms vs 63/75 in v127): the lock is taken only 100-150 times a frame.
- Translator experiment (static, no Vita run): recompiled recomp_0002/3/4 without the ebx/esi/edi reloads
  after calls -- 0-3% fewer instructions in the hot functions. XMM spill/reload around calls is ~1000 of
  2CA60's 4399 instructions but only a few functions use XMM (the postprocessor already limits it to the
  registers used). MEM32 is one table load + add. No cheap global win in the translator: the ~5 ARM
  instructions per x86 instruction are mostly the guest's own memory traffic.
- The sound manager's stop wait: 0x000EB080 calls StopEx (0x0012A011) and GetStatus (0x0012A079) up to
  10000 times until the voice reads stopped; 0x000ECA50 the same around 0x0012A035. The emulated APU
  finishes a stop only at its next 5.3 ms frame, and every poll went through DSound's locks and DoWork.
- v131: wrappers on both (gen renamed *_gen); inside the stop wait, a voice still stopping (flags 0x8001)
  is force-stopped at once with DSound's 0x0012DA17. TT_FASTSTOP=0 disables. [SNDWAIT] every 5 s.

## v131 result / v132 -- films on the video decoder
- v131 log: ~20 voice stops a second, 1-12 ms per 5 s -- small; DSound share in crowds 10% (v129: ~22%),
  heavy frames p90 84 ms (v130: 102). Gameplay is now flat game code.
- v132: src/hwmovie.c plays ux0:data/twotowers/movies/<name>.mp4 (H.264 baseline 640x480 + AAC, made with
  ffmpeg from the .bik) with sceAvPlayer when the game opens <name>.bik. Hooks (twotowers_manual.c, gen
  renamed *_gen in recomp_0032.c): BinkOpen 0x13E9E0, BinkDoFrame 0x140190 (no decode, waits for the
  player's clock; at its end puts HBINK+0xC at frames-2), BinkCopyToBuffer 0x13F950 (NV12 -> YUY2 into the
  game's texture), BinkSetVolume 0x140CD0 (0), BinkClose 0x140990. Sound: its own BGM port.
  TT_HWMOVIES=0 disables. First film converted: logos.mp4.

## v133-v139 -- the films on the video decoder work
- v133: the .mp4 looked for in several folders. v134: sceAvPlayerInit's handle is an address (0x8xxxxxxx,
  negative as int): only 0x806Axxxx is an error. v135: inactive before it first plays is not the end.
- v136: no system memory block left for the decoder's frames (vitaGL holds CDRAM and phycont): taken
  from vitaGL's pool (vglForceAllocAligned). vita-elf-create needed room after segment 0 (a 16 KB pad).
- v137: the film textures not checksummed (a version number); v138: hwmovie.c gives ARGB (the
  texel-at-a-time YUY2 decode was ~57 ms a picture) -- 11 fps shown.
- v139: gtex_get draws the film from the decoder's NV12 frame: a vitaGL VGL_YUV420P_NV12_BT601 texture
  with vglOverloadTexDataPointer to the frame (no decode, no copy, no upload). 3 output buffers.
  TT_HWMOVIE_YUV=0: the v138 RGBA path.
- All 35 films converted (H.264 baseline 2.5 Mbps + AAC 160k; death.bik has no sound): 721 MB in
  LOTR DECOMP/movies.
- v140 (RGBA texture pointed at our own GPU memory with vglOverloadTexDataPointer): black too -- not the
  YUV format. v141: gtex_get makes two GL_RGBA textures filled with real (zero) data, gives their memory
  (vglGetTexDataPointer, stride a multiple of 8 texels) to hwmovie.c, which converts each picture into
  the one not shown (g_hwm_show). The user: films at 30 fps.

## v142 -- films without their .bik
- A film with an .mp4 opens game_files/_hwm.bik instead (a copy of death.bik: 24 frames, no sound), the
  guest name rewritten to "d:\_hwm.bik" for the call and put back. HBINK+8 (frames) = 0x7FFFFFF0; the
  player says when it ends. BinkNextFrame 0x1407D0 hooked (renamed _gen): for that handle only +0xC++.
  BinkOpen flag 0x04000000 (the second player, 0x000E9200/0x000E9270, no end check) -> sceAvPlayer looping.
  No player after all: frames = 2 (the loop ends). Without _hwm.bik: the real .bik as in v141.
- v143: the film loop paced by the display's vcount (a new picture every 2 refreshes) instead of the
  player's clock (presents fell 1 and 2 refreshes apart in turn).
- v144: the game's own film textures filled with YUY2 black (0x80108010) at their first CopyToBuffer:
  before the first picture and after the last the game draws them (zeroes = a green flash).
  [PROFINC] (prof_vita.c): return addresses on the game thread's guest stack per sample -- inclusive
  time by call site, every 10 s.

## v145 log (inclusive profile) / v146
- [PROFINC] worked (v144 rejected stacks above 16 MB). Caveat: the ctx esp is only stored at calls, so a
  caller's time after a call returns still counts under that call's return address; values that are not
  return addresses filtered with the list of the gen code's pushed return addresses.
- Two kinds of scene. Level 2 (gat): the render thread at 98-117 ms wall but 62-65 ms CPU, 54% of its
  samples inside vitaGL's glDrawElements (waiting on the GPU); the game thread 36% in 0x000E00E0's
  Sleep loop (0x4B7601, cleared by the D3D callback 0x000DFFA0 when the GPU finishes the frame): GPU-bound.
  Levels 3/4: game-bound (game 75-90 ms, render 52-58): world update 0x0F2FF0 ~27%, the sound manager
  under 0x0C4090 (0x0EA620/0x0EF440, 0x0EC620, 0x0ED800...) ~13%, 0x03A170 ~12%.
- v146: internal resolution on the Vita from TT_RES (default 0.75: 480x360 targets; 0.5..1).

## v146 result / v147
- v146 (0.75): level 2 median 80 -> 67 ms, p90 114 -> 94; level 3 unchanged (75 ms). glDrawElements
  in the render thread 43% -> 33%: 44% fewer pixels took ~a quarter off the GPU wait -- not only fill.
- Own-time profile by region, level 3 crowds: DirectSound library 15% of the game thread (plus the
  IRQL/lock wrappers), the game's render code 15%, CRT/D3DX ~9%, D3D 4%.
- v147: TT_RES default 0.85 (544x408). DirectSound's C entry points 0x13495E..0x134C45 (13 setters,
  stdcall, buffer first; gen renamed *_gen in recomp_0031.c): a call with the same arguments on the
  same buffer within 500 ms of the last real one is answered S_OK; pointer-looking arguments always go
  through. TT_DSDEDUP=0 disables; [DSDEDUP] every 10 s.

## v147 result / v148
- v147: [DSDEDUP] only 25-60 setter calls a second, values nearly always new -- no gain (left in).
  DirectSound's 15% is its own periodic voice work (run on the game thread when it lowers IRQL).
- Static check: non-volatile guest integer accesses in recomp_0002/3 -- under 2% fewer instructions.
  The hot game functions (2CA60: 777 insns, 61 calls incl. per-bone animation callbacks) would give
  ~1-2 ms each rewritten by hand.
- v148: frame pacing on the Vita (nv2a_pb_exec.c frame_limit -> vita_pace): presents on a grid of
  display refreshes, 3 (20 fps) or 4 (15 fps) chosen every 20 frames (>5 late at 20 -> 15; >=18 of 20
  fitting 3 refreshes -> 20), films 2; vsync on with it. TT_FPS=30/20/15 fixed, TT_FPS=0 off.
- v149: 0x000F79C0 (the scene's visibility test, with 0x000F7710's box corners) written natively
  (twotowers/src/f79_native.inc), checked against the lifted pair on the host: 1,000,000 random views
  and boxes (933k out, 52k partly, 14k in), no difference in eax, outcode or esp. TT_NATIVE_VIS=0: lifted.
  0x000FF7BC (_ftol2) natively: 2,000,000 values incl. NaN/huge and both rounding modes, no difference.
  TT_NATIVE_FTOL=0: lifted. (The native line-triangle test 0x3FD30 passes the old harness too -- still off
  by the user's call.)
- v149 broke the game (geometry torn, one hit killing): the native _ftol2 left its value on the x87
  stack -- the lifted one pops it (fstp, g_fp_top + 1), and the callers push before the call and do not
  pop after. The host test compared eax/edx/esp only; now it compares g_fp_top, the stack slots, ebp too
  (the v149 native fails it on every case, the v150 one passes 2,000,000). The F79 test checks the x87
  state as well (the lifted one only writes a free slot).
- v149's pacing read TT_FPS -- the game's own 30/60 setting -- as "30 fixed": the switch is TT_PACE now.

## v153-v154
- v153: 0x48CD0 (particle quad builder) native (twotowers/src/n48_native.inc), host-checked against the
  lifted one on 1,000,000 cases (memory, registers, x87 slots, g_ebp). TT_NATIVE_PART=0: lifted.
  v153 log: 0x48F30 inclusive 11.8% -> 5.4% in a crowd.
- v154: profiler frames in each thread's register struct (RecompRegs.r_prof_top, volatile stack frame):
  no thread test, no shared globals. TT_PROF_NATIVE(tag) (tt_prof.h) puts hand-written hooks in the
  profile: F1000000+id tt_hk, F2xxxxxx native wrappers.
- v154: pose LOD (0x2CA60 wrapper): a figure more than 2.5x as far from the camera as the hero keeps its
  pose every other request (5x: two in three), via the function's own "done" flag [+0x90]. TT_ANIMLOD=0.
- v154: native line-triangle test 0x3FD30 on again (host harness with x87 state, 120k cases, same).
  TT_NATIVE_RAY=0: lifted.
- v155: indirect calls' fast path (recomp_types.h RECOMP_ICALL_TRY_FAST): the flat dispatch table with
  twotowers_lookup_manual's overrides merged in at recomp_dispatch_init; a hit is one bounds check and a
  load, a miss takes the old full path. TT_NO_ICALL_FAST=1: old path. Native 0x3FD30 off again (user's
  call). ANIMLOD v154 kept nothing (no figure beyond 2.5x): thresholds 2x / 4x, [ANIMLOD] histogram.
- v156: 0x0011A830 (the fence after every draw) native in d3d_native.inc (D3DN_ENTRY: TT_D3D_NATIVE=2
  checks it live), host-checked on 60k cases incl. both calls (make-space, kickoff) and their
  registers. D3D natives and KickOff tagged in the profile (F2xxxxxx). [ANIMLOD] says which pointer is
  missing (v155: every figure "without a figure or camera").
- v157: file read-ahead (kernel_file.c) fetches the next 128 KB of a stream on a thread of its own
  (pread; RECOMP_PREFETCH=0 off); host stress test 400k reads, two streams, forgets: all equal.
  ANIMLOD takes the figure's place from 0x2CA60's first argument (its world matrix, +0x30).
  Profile tags off the tiny NEON helpers (FF200/FF246/FF344).
- v158: twotowers_manual.c's guest registers through recomp_ctx() (g_eax ... redefined after the
  includes; vita_emutls.c keeps the emutls variables in the same struct): every hook's eax/esp was an
  emutls lookup call. The NEON helpers FF200/FF246 and the particles' matrix product by lanes
  (vmul/vmla .f32 q, q, d[x]): v76's vgetq_lane + vmulq_n moved each lane NEON -> ARM -> NEON.
- v159: the game a frame ahead of the GPU (0xE00E0 / 0xDFFA0 wrappers, TT_GPU_AHEAD=0 off, [AHEAD]
  line): the frame-end wait no longer holds the game while the render thread draws the frame just
  finished; with two frames undrawn it waits for the older. Direct draws' index range by NEON.
- v160: TT_GPU_AHEAD off by default (v159: never triggered -- the game is already a frame ahead
  through the ring; its waits are for the frame before, the render thread being the slower).
  Finer render-thread zones in the present; [READS] line (what the file reads are).
- v161: dynamic resolution (nv2a_gl.inc vgl_dynres): the present's clear waiting on the SGX > 15% of
  the frame for 2 s -> 0.72, < 3% for 8 s -> back to TT_RES; 10 s apart; play only. TT_DYNRES=0 off,
  =0.65..0.84 the lower step. Read-ahead misses read with the lock released; a forgotten slot being
  filled stays its filler's (v157-v160 could hand one buffer to two reads). Native 0x3FD30 on again.
- v162: dynamic resolution off by default (v161: figures gone every other frame after the step). TT_DYNRES=1 on.
- v163: read-ahead asks for the next piece at once (6 slots).
- v164: a read across a read-ahead piece's end served from the next piece (v163: every seam a 128 KB miss).
- v165: pose LOD past 1.1x the hero's camera distance (every other), 2x (2 in 3), 4x (3 in 4): poses were 20% of the crowded level's game thread. TT_ANIMLOD=1: v161 steps.
- v166: pose LOD never within 1.5x the camera-hero distance of the hero; past 1.5x from the camera every other, 2.5x two in three, 4x three in four.
- v167: present sharpening on the Vita (Cg PRESENT: 4 neighbours, clc_f.zw the texel), default 0.8, TT_SHARP=0..2.
- v168: mixbin dedupe (0x129F79 / 0x129F95: same buffer, same pairs, under 500 ms -> S_OK here). [MIXDEDUP]. TT_DSDEDUP=0 off.
- v169: read-ahead keeps the descriptor's position pending (no lseek per served read: it waited behind the fetch's pread on the same file).
- v170: vertices copied once (TT_VCOPY1=0: off). vitaGL's _glDrawElements_CustomShadersIMPL copies, for every client-array attribute, stride x (top index+1) bytes into its frame pool (one vgl_reserve_data_pool + memcpy per attribute, disassembled). The direct path now copies the guest span all attributes read (lo..hi, a joined list's whole range) once, right before the draw, into the same pool (vgl_reserve_data_pool, same lifetime), and points each attribute at an object g_dz_obj ([0] = that memory, the VBO path: no copy) with its offset in the span. Only when the span is no larger than vitaGL's copies, within one 16 MB slice, <= 1 MB; else back to client arrays. [VCOPY1] in the log.
- v171: the one copy (v170) with sceClibMemcpy, as vitaGL copies. v170 log: glDrawElements 15.4% -> 10.5% of the render thread, but the copy itself 7.2% (newlib memcpy into the pool): no net gain.
- v172: [BONES] census (TT_BONECENSUS=0 off): the functions in a skinned object's per-bone table [+0x5E4] (rotation, position samplers called by 0x2CA60's bone loop), counted per bone, every 10 s -- to pick what to write natively.
- v173: [PROF] split of 0x2CA60 by section (sub_0002CB90 the bone loop's calls, sub_0002CC03 its matrix math, sub_0002D13A/D140 after the loop; the rest stays sub_0002CA60) and FF200/FF246/FF344 (NEON helpers) tagged. v172 [BONES]: ~1000 bones a frame in the crowd (17 poses), 2CA60 own 9.9% = ~7 us a bone; the samplers (2DFF0, 2E090, 2E130; positions 2E030 85%) cheap (not in the top).
- v174: 0x2CA60's bone math keeps each 4x4 product's rows in registers (the reloads of the rows just stored, L_eax == L_edx, replaced by the values; stores kept, so aliasing stays as before) and prefetches the next bone's base matrix (edi+0xC0). v173 split: the math (2CC03) 6%, the per-bone calls 2.6%, FF246 1%. LiveArea: icon0 (cover, fitted to the bubble's circle), bg (Helm's Deep), startup (the title), template.xml.
- v175: in 0x2CA60's bone math the lane-0 ops (subss x4, movss x5) done in NEON (vbslq with a lane-0 mask: no NEON<->VFP transfers) and its rcpps one division (the lanes are lane 0, splat before). v174 log: 2CC03 6.2% -> 5.0%. LiveArea: missing bg/startup = installed over an existing app (the Vita keeps the old LiveArea): delete the bubble, reinstall (saves in ux0:data/twotowers).
- v176: finer [RTPROF] zones (40 listed): sw3d stages/texture use/factors/combiners; gpu_bind_stages: combiner words, surface lookup, per stage, sampler, shader key, combiner constants upload, GL texture binds. v175 log: big scenes (1100-2000 draws) render-bound (render 48-54 ms, game 21-23 ms waiting in F8EB3, a kernel wait through the import at 0x17000C); crowd (850 draws) game-bound (65-70 ms). 2CC03 unchanged ~5%.
- v177: TT_PROFILE (CMake option, default ON): OFF builds the generated code without RECOMP_PROFILE (no frame per call), TT_NO_PROF (no sampler, no TT_PROF_NATIVE frames); build-rel is that build, to measure the profiler's cost. tt_seg0_pad (8 KB rodata): vita-elf-create needs ~4.5 KB after segment 0 (the smaller build was 856 bytes short).
- v178: [FXCENSUS] every 10 s: particle quads (0x48CD0) and weapon trails (0x24C80's 13 slots at 0x1FE7C4, 0x370 apart, count at +0, first point at -0x35C; gen renamed sub_00024C80_gen) by distance bin (hero's own <2 units, near him <1.5 camera-hero lengths, camera <1.5/<2.5/<4/more). TT_FXCENSUS=0 off. Back to the profiled build (v177 showed the profiler costs nothing measurable).
- v179: the render thread spins (yield, TT_RT_SPIN iterations, default 20000, ~0.1-0.2 ms) before sleeping: a kick (after every draw, ~60 us apart) that found it asleep paid a SetEvent on the game thread (KickOff 5-7% of the game thread, v178). [RTWAKE] per frame: wake-ups, waits ended awake, sleeps. v178 [FXCENSUS]: ~80% of particle quads and all weapon trails near the hero -- no distance cut worth it.
- v180: [GXMDRAW] (--wrap=sceGxmDraw): per frame the draws, their time, and those over 0.1 ms (the GPU's command ring full: GPU-bound). TT_RT_SPIN default 0 again (v179 log: 1-3 wake-ups a frame; KickOff's cost is the command scan, 7 ms a frame in Moria). v179 Moria log: render-bound, render thread 74-77 ms (wall 87), glDrawElements 38% of it.
- v181: the sound chip's thread pinned to core 2 (TT_SND_CORE=mask hex, 0: leave), read-ahead and profiler threads to cores 0|2: pthreads made with the defaults could run on the render thread's core 1 at the same priority (SetThreadPriority is only tracked). [THREADS] per report: each thread's cpu, mask, priority, preemptions and moves per frame. v180 [GXMDRAW]: sceGxmDraw never waits (0 over 0.1 ms): Moria is not GPU-bound; the render thread ran ~75 of ~100 ms wall, not idle.
- v182: v181's core 2 alone starved the sound thread (18-100 periods/s, crackle: the game's thread shares 0|2 at the same priority). Now sound on cores 1|2 at priority 140 (TT_SND_CORE, TT_SND_PRIO), the render thread at 128 (TT_RT_PRIO) above it, the game at 159: real priorities (SetThreadPriority was tracked only, all at 159).
- v183: [RTPROF] zones inside vitaGL's glDrawElements: wraps of vglCreateVertexProgram, sceClibMemcpy, sceGxmSetVertexStream / FragmentTexture / (Default)UniformBuffers, scene_reset, and the existing sceGxmDraw / data pool / Set*Program wraps set g_vgl_where ("vitaGL: ..."); our own span copy calls __real_sceClibMemcpy. Constants run: one pass, only the differing words copied and their rows marked (was memcmp + whole copy). v182 Moria: frames -15..20% at equal draws (real priorities).
- v184: the combiners' decode (tex_usage, comb_prepare, comb_compile) only when combiner registers (GRP_COMB/OTHER) changed: when only textures did, kept (TT_COMB_ALWAYS=1: every time); [VGLPERF] combiners decoded/kept. [MEMCPY]: sceClibMemcpy's callers by return address and KB per frame (vitaGL: sceClibMemcpy 5.6% in Moria, v183). v183: vertex constants run 8.4% -> 1.9%; the sampler off core 1 (v181) made [RTPROF] unbiased: glDrawElements 2.9%.
- v185: DirectSound's object lock/unlock (0x12C3AD / 0x12C3E0, thiscall) native: xbox_KfRaiseIrql(2) / xbox_KfLowerIrql directly instead of the import thunks (0x17010C = ordinal 160, 0x170084 = 161, checked in the XBE) through the indirect call and the bridge; same registers (eax old level / 0, ecx, edx, esp+4) and memory. TT_DSLOCK_NATIVE=0: the lifted ones. v184 Moria (game-bound now: game 54-61 ms, render 42-52): DSound code ~23% of the game thread (12E5C6 6.5, 12C3AD 5.0, 12E505 4.0, 12D2B9 3.0, 128C07 2.7, 12C3E0 2.0). Combiners: ~450 a frame kept, ~60 decoded.
- v186: [GXMSCENE] (--wrap sceGxmBeginScene / EndScene): scenes a frame and the time BeginScene waits (blocks while the GPU is too far behind). v185 Fangorn: in the heaviest frames (70-100 ms) the render thread waited ~25 ms a frame inside vitaGL's scene_reset (32% of its samples) -> sceGxmBeginScene: GPU-bound there. DSound lock natives: 12C3AD 5% -> 0.9%.
- v187: [TGTSEQ] every 10 s one frame's draw-framebuffer changes in order (fb, its height, draws before the change, the zone that switched). v186: sceGxmBeginScene never waits (<1 ms a frame); sceGxmEndScene is 35% of the render thread in the slow frames (render wall ~82 ms, ran ~53); 10-50 scenes a frame (median 12.75).
- v188: TT_GEN_OPT (CMake cache string): optimisation flags for the generated code only. build-o2 (-O2): text 13.73 -> 13.13 MB; build-os (-Os) for the A/B.
- v188 A/B (game code ms/frame by draws, vs v187 -O3): -O2 700-900 draws 40 (55-62), 500-700 38 (41-49); -Os worse (69 / 52). TT_GEN_OPT default -O2. v189 A/B: build-all2 (everything -O2) and build-o1 (generated code -O1).
- v189 A/B: all2 broke the audio (distorted: the sound chip's code at -O2; its period rate was normal) and gained nothing in Amon Hen; o1 (generated code -O1) no better than -O2 (crowds 2-3x the bones of the -O2 test, game code ~55 ms at 700-1200 draws). Base stays v188-O2.
- v190: XACT's DoWork (0x135CAB, 5% of the game thread in crowds) on a worker thread of its own (twotowers_manual.c, gen renamed sub_00135CAB_gen in recomp_0031.c): the game's request wakes it and returns; it runs the lifted function under XACT's own lock (0x134940, PASSIVE: the critical section 0x139F58), guest stack from xbox_worker_stack_alloc (xbox_worker_stack_top, new in kernel_thread.c), its own TIB. Core 2, priority 158 (TT_XACTW_CORE / TT_XACTW_PRIO); TT_XACTW=0 off. [XACTW] every 10 s.
- v190: the line-triangle test 0x3FD30 native the whole way (TT_NATIVE_RAYFULL=0: the lifted rest): the hit point (t clamped to 1), the three edge tests (0x1D840's product, the SSE dot, 0x1D800's value left in the x87 slot and its ecx = the dot's bits), host-checked against the lifted function (work/nt run4: registers, x87 slots and flags, memory). v189 log: 3FD30's lifted rest was 7% of the game thread (1200-1500 tests a frame).
- v191: v190 log: the ray test native the whole way out of the profile (3FD30 gone from the top); the XACT worker at priority 158 on core 2 was put aside by the sound chip's thread (140) while holding XACT's and DirectSound's locks: runs 3-9 ms (longest 195), and the game thread waited on them (DirectSound's lock 0x128C07 up to 16%, XACT's 0x134940 up to 18%). Worker priority 130 (above the sound chip, the render thread on core 1 unaffected).
- v192: v191 log: no lock waits left (128C07 1-3%) but no clear gain on the game thread, and crackle in crowds: the sound chip's thread runs 47-56 ms of a 57-66 ms frame there (~85% of a core, [CPU]) and the worker at 130 took core 2 from it. XACT worker off unless TT_XACTW=1. New [APUPROF] every 10 s (one sound frame in 4 timed): voices (fetch+resample, filter, mix), effects (dsp_frame), output (monitor_frame), voices per frame. HRTF is off (g_config.audio.hrtf false), so not it.
- v193: v192 [APUPROF]: ~28 us of the sound thread per voice per 32-sample frame (fetch+resample ~13, mix ~5.5, the rest the voice's register reads); 20 voices in a crowd = 600 of the 667 us of sound: the crackle, and the game thread waiting on the chip's lock (DirectSound's 12E5C6/12E505/12D2B9 + locks 20-38% of it). apu_phys_ptr's one-page cache was __thread (two emulated-TLS calls an access on the Vita, three pages taking turns): now a shared 1024-entry table of one-word entries (page, block, valid), reset when the kernel adds an identity page (g_id_phys_gen). The resampler's state in locals (samples[] stores aliased s_rs) and the next source sample straight from the chunk; a PCM voice's scatter entry read once a page.
- v193 log: audio fine (sound chip ~400 of 667 us in the worst crowds), DirectSound on the game thread 3-4% (was 6-13%), but the game's code still 54-56 ms at 700-1200 draws: the profile flat (nothing above ~5%).
- v194: profile-guided optimisation of the generated code (twotowers/CMakeLists.txt TT_PGO / TT_PGO_DIR). TT_PGO=gen (build-pgogen): -fprofile-generate=/pgo -fprofile-update=single on the gen sources, libgcov linked, main.c sets GCOV_PREFIX=ux0:data/twotowers (files under ux0:data/twotowers/pgo, names the mangled object paths), sub_000E00E0's wrapper runs __gcov_dump + __gcov_reset every 3 minutes ([PGO] line). TT_PGO=use: -fprofile-use=TT_PGO_DIR -fprofile-partial-training (code never run keeps -O2) -Wno-missing-profile -Wno-coverage-mismatch.
- v194 (cont.): the Vita toolchain's libgcov is the freestanding build (__gcov_dump/__gcov_init 4-byte stubs, value/time profilers empty): gen code built with -fprofile-info-section=gcov_info -fno-profile-values, libgcov for __gcov_info_to_gcda, and twotowers_manual.c writes each object's .gcda itself (ux0:data/twotowers/pgo/<mangled object path>.gcda, written over with the run's totals every 3 minutes). The names carry build-pgogen's path: the TT_PGO=use build must be in build-pgogen too (reconfigured), the files in TT_PGO_DIR. Instrumented: .text +4.6 MB, .bss +1.8 MB.
- v194 run: 6 dumps (18 min, 38 files) then a crash on the render thread in vitaGL's reload_fragment_uniforms (ffp.c:487, r0=0x8FC01000: a write past a mapped block's end); not in the PGO code; cause open.
- v195: TT_PGO=use build (in build-pgogen, reconfigured; .gcda in /home/claude/pgo, GCC finds the '#'-mangled names itself: -Wmissing-profile silent, and an unmangled copy changes nothing). .text 14.8 MB (plain -O2: 12.3 MB). The regular `build` dir is still plain -O2 v193 code + v194 sources.
- v195 log (444 MHz) and again at 500 MHz (PSVshell; the sound chip's cost per voice -12%, so it was on): PGO ~5% on the game's code (50-54 ms at 700-1200+ draws). In the worst Amon Hen frames (72-86 ms) the game thread spends 27% in 0xE00E0's wait for the GPU's frame end (Sleep, 0xF8EB3) and the render thread 31% in sceGxmEndScene (9 scenes a frame, main target 4 times): render/GPU-bound there, not the game's code.
- v196: --wrap sceGxmCreateRenderTarget: scenesPerFrame 4 for targets >= 400 lines, 2 for the small ones (vitaGL's is logged; fallback to its own if refused), TT_SCENES=<n>. Hypothesis: with one scene a frame per target every revisit waits for the GPU. Built in build-pgogen (PGO use).
- v196 log: scenesPerFrame confirmed (sceGxmEndScene wait 11.9 -> 0.9 ms a frame, <=4% of the render thread), but the wait moved to vglSwapBuffers (25-37%): the user's env.txt has TT_PACE=1 ([PACE] line, also in the v193/v195 logs), which turned vsync on: frames of 52-55 ms shown at 66.7 (15 fps), 68 at 83 (12 fps).
- v197: pace mode no longer turns vsync on (TT_PACE_VSYNC=1 for the old way); the automatic pace stays at 20 (the drop to 15 only with TT_PACE_15=1). Early frames still wait for their refresh in vita_pace; late ones go out at once.
- v197 + TT_RES=0.5 (320x240): the swap's wait for the GPU gone (GPU-bound frames 59-71 -> 45-54 ms); game-bound frames (63-67 ms, Amon Hen's worst) unchanged. So with scenesPerFrame fixed the GPU is pixel-bound at 544x408. (The [PACE] line prints even with the pace off: v197's premise was wrong, the user has no env.txt.)
- v198: mipmaps ON by default on the Vita (were off: every far surface read a full-size RGBA8 texture -- the suspected per-pixel cost; RECOMP_MIPMAPS=0 off), min filter LINEAR_MIPMAP_NEAREST (TT_TRILINEAR=1 for both levels), the Vita keeps its own mip/aniso choice in the settings pass. [AB]: every 10 s the samplers are remade with / without the mip levels read (g_vita_mip_sample), and a line gives frames, ms a frame and the swap's wait for each turn (TT_AB=0: no turns). TT_WIDE=1: xbox_GfxAspect 16:9 on the Vita (untested there).
- v198 log ([AB], 10 s turns): light scenes the swap's GPU wait 5.4-5.9 -> 0.1-0.6 ms with the mip levels read; in two adjacent heavy pairs 54.9-56.7 ms read vs 68.3-72.2 not read; game-bound stretch 61.5-69 vs 71 (no difference). Mipmaps stay on. TT_WIDE had no effect on the picture: gpu_apply_settings (which reads xbox_GfxAspect) never runs on the Vita.
- v199: g_aspect from xbox_GfxAspect at the renderer's start on the Vita ([GL] picture line); [AB] turns only with TT_AB=1.
- v199 log: [GL] picture 1.333 -- TT_WIDE not seen (env.txt not read or mistyped; the [ENV] lines print before the log is open). v200: 16:9 the default on the Vita (TT_WIDE=0: 4:3); env.txt names/values trimmed, a UTF-8 mark skipped, and what was read printed again at the renderer's start ([ENV] env.txt: ...).
- v200 log: still 4:3 -- twotowers_host.h always sets RECOMP_ASPECT from the ini ("4:3"), and v199's init took xbox_GfxAspect only when RECOMP_ASPECT was unset. v201: the Vita's start sets RECOMP_ASPECT / TT_ASPECT = 1.7778 unless TT_WIDE=0 (main.c), and the renderer's init takes xbox_GfxAspect regardless.
- v201 log: the renderer wide (725x408 targets, picture 1.778) but shown 4:3 and slower: no [WIDE] line, the game's SetPerspective hook (0x4E890) never widened a camera (g_ws_cam_tick stayed 0). v202: 16:9 only with TT_WIDE=1 again; the hook logs its first 64 different calls on the Vita (fov, aspect, screen, in-scene, factor, caller).
- v202 log: every logged SetPerspective call was "in a scene" (tt_in_scene: HUD not drawn in the last 300 ms) -- the play camera's projection is set once at its shot's start, before the HUD's first draw, so the PC's "scenes stay 4:3" rule kept every camera 4:3 on the Vita. v203: on the Vita all screen-6/8 cameras widened (scenes too); 16:9 the default again (TT_WIDE=0: 4:3), default TT_RES 0.74 when wide (631x355, the pixel count of 544x408).
- v203 log + videos: 16:9 works ([WIDE] cameras widened, full screen). Stray dots along the bottom row and a right column: 0.74 gives 631.5 x 355.2. And the 355-line main target fell under the 400-line rule: 2 scenes a frame, the GPU wait back (24.6 ms at 900-1200 draws). v204: wide default 0.75 (640x360, x1.5 to the screen), 4 scenes from 300 lines.
- v204 log (Amon Hen + all of Fangorn, 692 s of play, 640x360 wide): dots gone. Frames: median 55 ms (18 fps), p25 48, p75 61, p90 69. Of the 458 s at 50 ms or more, 393 are GPU-bound (game's code 31-36 ms, then 15-26 ms waiting; the swap 17-35% of the render thread), 65 game-bound. The GPU, not the game's code, is the limit most of the time now.
- v205 (measuring): [AB2] 8 s turns in play -- everything / alpha-tested draws skipped / blended draws skipped -- with frame time and swap wait per turn, and turn 0's census (draws and indices: plain, alpha-tested, blended, both; depth writes off; alpha funcs). TT_AB2=0 off. The old mip [AB] removed.
- v205 census (in play): plain 12-13 draws / 550-830 indices a frame; alpha-tested 417-485 draws / 52-62 thousand indices; blended 36-129 / 1.3-7.3 thousand; both 479-1196 / 3.3-6.9 thousand; alpha funcs GREATER and GEQUAL. Nearly the whole world is drawn with the alpha test on: every such shader has a discard, so the SGX gives up hidden-surface removal for it. (The turns' timings were of game-bound moments, not usable; the user found the vanishing picture broken.)
- v206: GTex.opaque (all texels' alpha 255, scanned at upload); in gpu_bind_stages the alpha test is left out of the shader key when the draw does not blend and every texture it reads is opaque (surfaces and pack pictures count as see-through). TT_ATEST_KEEP=1: as before. [AB2] now alternates this on/off every 8 s (same picture), with frame time, swap wait and the count of setups dropped/kept. TT_AB2=0: always on.
- v206 logs: the plains of Rohan are draw-bound (1200-2000 draws a frame, game's code 54-80 ms, GPU wait ~0): the alpha-test turns show nothing there. Fangorn: swap wait 7.6 -> 2.2 ms with the test dropped, frame time the same (54.9 vs 54.6): the game's code (42-70 ms) is the limit now. Game ms ~ 25 + 20 us x draws over the last logs. In the plains' heavy block the sound manager (C4090 -> EF440 -> EEC60) is 13.5% inclusive.
- v207: the sound manager every other frame when the frame is 45 ms or more (sub_000EF440 wrapper, gen renamed _gen in recomp_0018.c; both frames' time passed in 0x4B8B98/0x4B8BA0 for that call; XACT's DoWork still every frame). TT_SND_HALF=0 off. The alpha-test fix always on, [AB2] turns only with TT_AB2=1.
- v207 log (Rohan's second level): 2300-3000 draws a frame; the render thread is the limit (90-122 ms a frame, ~36 us a draw, flat: vertices copied 8.8%, vitaGL memcpy 6.9%, shader key 5.1%, program 4.5%), the game's code 63-75 ms then 25-45 ms waiting for it; GPU wait ~0. 53% of the time under 12 fps. Not drawn per frame there: 214 effect quads, 692 round shadows, 88 lights' draws (still prepared by the game and parsed). Vertices in place was already tried and failed (v123/v124: wrong geometry, a freeze) -- not repeated.
- v208: the camera's far end x TT_FAR (default 0.75: 300 -> 225) in sub_0004E890's wrapper for screens 6/8 ([FAR] line). TT_FAR=1 off.
- v208 log, by level ([1BB2A4]): 7 (Rohan 2) median 83 -> 53 ms (12 -> 18.7 fps), mean draws 2042 -> 1516 with the far end at 225 (different stretches of the level, an indication); 4 (Amon Hen) 63 ms; 9 (Helm's Deep 1) 94 ms (10.7 fps), game-bound: game's code 91 ms at 926 draws, sub_0004BC20 13.8% own, 458D0 6.7%, 5400-5800 ray tests a frame.
- v209: 0x4BC20 (the rain: N items of 0x30 bytes -> 2 coloured vertices each -> a line list through 0x1181A0) native (native_hot.c tt_nat_0004BC20, gen renamed _gen in recomp_0005.c): the two colours made once instead of 8 float-to-integer calls an item. Host-checked against the lifted one (work/nt run_bc: 600 cases, registers, x87 slots, memory, the draw's arguments). TT_NATIVE_RAIN=0: lifted.
- v209 (no log yet). Web: CapUnlocker (kernel plugin) gives game apps the fourth core; PowerVR's own guide confirms discard/alpha test defeats hidden-surface removal (v206). The user's vitaGL-master.zip (Rinnegatamante/vitaGL) is the same version as the vdpm libvitaGL.a (984 global symbols identical, sizes within 2 KB: the stock build had no options).
- v210: libvitaGL rebuilt with NO_DEBUG=1 (SKIP_ERROR_HANDLING) and HAVE_VERTEX_LAYOUT_CACHE=1 (the vertex program and streams of a program's layout reused); the stock lib kept as libvitaGL.a.stock in the SDK. Not the draw speedhacks (they let the GPU read client memory: the v123 failure). The sound chip's thread asks for core 3 alone (0x80000; [APU] the fourth core line; refused: 0x60000 as before; TT_SND_CORE overrides); g_core3_ok. With it, the XACT worker (v190) is on by itself on core 2 at priority 158 (TT_XACTW=0 off).
- v210 log: core 3 granted (the sound chip there, cpu 3 mask 80000), XACT worker on core 2. But 5 stalls of 1-2.8 s and one of 11.7 s (render thread stuck; at new render targets, a load, a scene), the user saw slow loads and frozen cutscenes; fps no better (Amon Hen 61 vs 63 ms median, Helm's Deep 80 vs 94 = the rain). Likely: threads made with the default affinity (game threads, vitaGL's GC at default priority, affinity 0) placed on core 3 behind the sound chip at 140. v211: everything of v210 off by default -- the stock libvitaGL back in the SDK (the fast one kept in /home/claude/vgl-src/fast), sound core 3 only with TT_SND_CORE3=1, XACT worker only with TT_XACTW=1. = v209 behaviour.
- v211: = v209 behaviour (loads fine again, the user says). Helm's Deep (v210 log, level 9, in play): inclusive 0C79B0 (the entity updates, virtual calls) 18%, F1310 10%, B9530 9.6%, 044760 9.1% (collision), 0522E0 8.4%, 0458D0 7.4%.
- v212: 0x458D0 (a line against every triangle of a mesh, the nearest hit kept in the line's record) native (native_hot.c tt_nat_000458D0, gen renamed _gen; TT_NAT entry). The same 0x3FD30 calls with the same stack and registers. Host-checked (work/nt run_58: 3000 meshes, 1072 with a nearer hit kept, 148 thousand line tests; registers, x87, memory). TT_NATIVE_MESHRAY=0: lifted.
- v212 log, medians by level: Amon Hen 55 ms (18.3 fps; 61-63 before), Helm's Deep 76 ms (13.1 fps; 80 with v209's rain, 94 before). Up to 16 thousand ray tests a frame there.
- v213: the far end cut off by default (TT_FAR=0.75 for v208's): the user found the vanishing far world not worth it.
- v212/213 profile: 0x46040 (every sector's every mesh against a line: 0x458D0 for each) 7.5% own in Helm's Deep (the native 458D0/3FD30 have no profile frames: their time shows in 46040), 46040+46B80 6.5% in Amon Hen. No spatial test before the triangles.
- v214: each mesh's reach box (native_hot.c nh_mesh_box / nh_tri_reach): per triangle the three edge half-planes of 0x3FD30's tests (tolerance -0.005) in its plane; bounded only when the edge normals are not all on one side (then the region is the triangle of the three corners, or empty); the corners' box + 0.05 + 1e-4 x extent. A line whose stretch P + tD, t in [-0.0011, 1], has an axis outside the box is skipped before the tests (ray record exactly as the tests leave it; ecx / x87 flags are scratch). Unbounded / degenerate / NaN: the mesh never skipped. Static check: the triangles' words summed every 4th use, two changes = never skipped. 2048-entry table. Host-checked (run_58: 3000 cases x 2 kinds, 248 calls skipped, ray records and memory identical). TT_MESHCULL=0 off. [MESHCULL] with [PERFG].
- v214 log: Helm's Deep's walls broken -- the siege ladders never arrive, the ladder HUD never moves, the level stuck. [MESHCULL]: ~30 mesh tests a frame, 3 skipped, almost no triangles saved (the 46040 cost is many meshes of few triangles, or near ones). v215: the cull off unless TT_MESHCULL=1 (likely cause: the ladders' moving meshes against a box stale for its first uses). If the ladders still fail: TT_NATIVE_MESHRAY=0 (v212's loop) next.
- v215 log: Helm's Deep ladders fine again (user). Rohan 2 still render-thread bound (~800 direct draws, 428 lists a frame; [VCOPY1] 1.6 MB copied a frame; RTPROF "direct: the vertices copied" 12.7%, vitaGL sceClibMemcpy 6.7%).
- v216: the static vertex cache (nv2a_gl.inc vc_find / ix_find / dz_draw). A dz span (guest start, length; 256+ bytes) whose bytes hash the same (64-bit, NEON, host-checked: 6000 single-bit/sign/swap/zero changes all detected) in two frames is copied once into an arena of ours (6 MB from vitaGL's RAM pool, TT_VCACHE_MB; mspace) and drawn from there; every use hashes the span again, a changed span is copied into the frame pool as before (never stale); its old copy freed 4 frames later; 3 changes = dynamic for 600 frames (no hashing). Triangle lists (96+ indices) the same in two frames: kept (content-addressed) and drawn from an element buffer of ours (g_ix_obj). Direct draws now glDrawRangeElements with the top index (vitaGL no longer scans the indices). Arena full: copies unused for 6 frames freed. vitaGL's frame pool 32 -> 26 MB (busiest frame seen 5.7 MB of 8.7). [VCACHE] line after [VCOPY1]. TT_VCACHE_MB=0: off.
- v216 log: worse. Rohan 2 render 75 -> 83 ms (median), Amon Hen frame 54 -> 65 (game thread 47 -> 55: the extra reads compete for memory). RTPROF: "direct: vertex cache check" 13.7% against the 9.7% the copy cost -- the copy is bound by READING the guest's (cold) vertex memory, not by writing; hashing every use reads as much. Only 3 MB of arena found (RAM pool), cache thrashing (hundreds dropped/no room per 120 frames). But: 0 cached spans ever changed in the whole session (Rohan 2, Amon Hen).
- v217: a cached span checks one of its 16 stripes a use, in turn (each stripe its own 64-bit hash; 1/16 of the reads; a change anywhere seen within 16 uses, then copied as before and re-hashed whole); promotion still needs the whole span the same in two frames (and unchanged while copied). Arena 8 MB, vitaGL's phycont pool first, then RAM. Index cache and glDrawRangeElements kept.
- v217 log: Rohan 2 render 70 ms median (75 in v215), frame 75 (79); Amon Hen render 36 (44) but its game thread 58 ms (another stretch of the level: 800 draws against 1270). [VCACHE]: 0 cached spans changed.
- v217 capture (Rohan 2, burning village, 2152 draws as captured): 596 A3A21E8D scenery (205 of them single quads, many textures), 619 EB7D5109 foliage (one 64x64 texture, alpha-blended: order matters; its 180 single quads already not drawn by TT_EFFECTS=3), 274 9D5CFD70 flames/embers (unit quad placed by constants c104-106; 249 added on, SRC_ALPHA/ONE, no depth write, three 32x32 textures in random turn), 558+42 AA3EF38B (round shadows, not drawn; 42 added on). The dark burn marks on the ground are not draws of their own: they are in the terrain's second texture (80E97800 128x128), free.
- v218: the added-on single quads collected by texture (vsm_*, up to 4 buckets, the key without the textures: g_vsm_gk), each quad's 4 vertices through its program on the CPU, one draw per bucket before anything else touches GL (textures rebound per bucket; g_sh.tex_ok cleared after). RECOMP_VGL_QUADS=2 default (1 = v104's all-in-order, 0 off). TT_GLOWS=0: the added-on single quads not drawn at all.
- v218 log: the quads grouped as meant (e.g. 173 in 3 runs) but their CPU transform ("batch: indices") 28% of Rohan 2's render thread (render 70 -> 82 ms median), 10% + "after indices" 8% in Balin's tomb (render 64 ms; 43 in v197, 55 in v203: the wide picture shows more). The CPU path costs ~100+ us a quad, more than a draw. v219: RECOMP_VGL_QUADS default 0 again (v217's drawing); TT_GLOWS=0 kept as an option.
- v220: 4:3 again by default (TT_WIDE=1 for 16:9): main.c RECOMP_ASPECT/TT_ASPECT and gfx_menu.c xbox_GfxAspect only with TT_WIDE=1; the internal resolution back to TT_RES 0.85 (544x408).
- v220 log (4:3): Rohan 2 frame 59 ms median (75 in v217), render 52, game 45; Amon Hen frame 58 (game 53); Helm's Deep frame 76 (game 73). The game's profile flat (top own 6%); the render thread's too (vitaGL ~22%, of it ~7% the GXM calls themselves).
- Research (Xita, Halo CE Vita): the Vita ports get there mostly by giving the game less to do -- objects of a few pixels skipped, statics/lighting/sound checks updated less often, cheaper AI sight.
- v221: objects of a few pixels not drawn (twotowers_manual.c tt_tiny, in 0xF79C0's wrapper, only for the draw walk's object tests 0x42C4E / 0x42C09): the box's diagonal over its depth along the view, in pixels of 480 lines (camera, view direction and narrowest half-angle derived from the view's six planes: the opposite pair near/far, the four others meet at the camera; checked on a synthetic frustum), under TT_TINY (default 4) = out after the real test. Views the planes do not fit (parallel) untouched. [TINY] every 10 s: tested, skipped, under 2/4/8/16 px.
- v221 log: kept by the user (nothing seen missing). Rohan 2 frame 51 ms (59), Helm's Deep 71 (76), Amon Hen 58 (58). [TINY]: up to half the objects tested skipped, nearly all under 2 px (many likely zero-size boxes).
- v222: far enemies step every other frame (twotowers_manual.c tt_ai_lod in 0x843F0's wrapper): characters with vtable 0x1741C0 (not the heroes' 0x1743C8), on the ground (+0x2B4 = -0.1), over 1.5x the camera-hero distance from the camera and from the hero: a step skipped (esp += 8), the next run with both frames' seconds (its argument and 0x4B8BA0 while it runs). Never in co-op. TT_AILOD=0: off. [AILOD] every 10 s.
- v222 log: [AILOD] only ~5% of enemy steps far enough (1000-1700 of 16-28 thousand in 10 s), no gain seen (Rohan 2 57 ms, Amon Hen 60). A crash in Helm's Deep (dump truncated, .tmp: thread 0x40020461, a guest-made pthread (64 KB stack, prio 0x9F, cores 0/2), stopped 0x30004 at 0x81C0E090 = vitaGL's glVertex3f+0x84, which nothing calls: a bad jump; the game thread in sub_0011B1E0).
- v223: TT_AILOD off by default (=1 on). TT_ANIMLOD=3: pose LOD from 2.5x (every other), 4x (2 in 3), 6x (3 in 4) -- the middle distance smooth (the user: far enemies jerky).
- v223 log: game-thread bound -- Helm's Deep game 71 ms (it runs the whole frame, not preempted), render 33 (idle half the time); Amon Hen game 59. Non-volatile guest accesses tried on the host (recomp_0004.c): only 3% fewer instructions, the hot functions unchanged -- dropped.
- v224: the same ray twice (twotowers_manual.c, sub_00044760 wrapper; recomp_0004.c's renamed _gen): a ray record whose input words (+0x00..+0x30, +0x34..+0x4C) and caller are exactly those of one cast the frame before gets that answer (+0x30, +0x4C..+0x6C, the sector list 0x3C3DA8/0x3C3DAC up to 16, eax/ecx/edx) -- every other time, the next one cast again (a moving thing seen at worst a frame late). tt_gframe counted in 0xEF440's wrapper. TT_RAYREUSE=0 off. [RAYQ] every 10 s with the top callers. The actors' collision step for standing actors not done: 0xB9530 already drops moves under 0.01.
- v224 log: [RAYQ] 25-40 rays a frame, nearly all from 0xB9DF4 (in the actors' collision step), under 10% reused: no gain. Each ray ~250 us (the sector tree, then every mesh face).
- v225: (1) the sectors a ray crosses by hand (native_hot.c tt_nat_00046120 = 0x46120 + 0x45EC0 + 0x45C10 + 0x3C3E0, the x87 doubles/floats and NaN outcomes kept; host-checked: 10000+50000 random trees, 0 differ; only from 0x44760, ra 0x4478E, where registers/x87 after are dead). TT_NATIVE_SECT=0. (2) the pacing on by default in play only (tt_in_play: screen 6, HUD drawn, no film): 20 fps, 15 when more than a quarter of 20 frames miss 20 (TT_PACE=0 / TT_PACE_15=0). (3) the swing's arc at low frame rates (sub_000BA110 wrapper, tt_sweep_arc): the player's weapon (id 1) missing in a frame of 45 ms or more is asked again in two halves with a midpoint matrix (rows: normalised means, re-orthogonalised, mean lengths; place: turned about the player on x/y, height halfway). Query only (no damage repeated). TT_SWEEPARC=0. [ARC] every 10 s. TT_RAYREUSE now off unless =1. A 4 KB pad in .text (native_hot.c) so vita-elf-create has room before the data segment.
- v225 log: Helm's Deep game thread 58 ms median (71-80 in v223/v224), frame 64 -- held near 15 by the pacing, which switched 20<->15 119 times; Amon Hen 63 (game 56), Rohan 2 56. [ARC]: 5-40 long-frame misses re-asked per 10 s, 0-3 hits found. v226: pacing off again by default (TT_PACE=1 on).
- v227: the mesh-ray loop (tt_nat_000458D0) passes the faces the ray's mask turns away in a tight loop (one host pointer for the face array when linear, mask and count read once between tests, prefetch 8 faces ahead); host-checked (run_58: 3000 cases x 4, 0 differ). The native sector walk also for 0x447A0 (ra 0x447C7, before 0x460B0).
- v227 log (Helm's Deep 1-3, levels 9/10/11): 9 frame 57 ms (game 55); 10 frame 67, render-thread bound (1365-1500 draws, render 63 ms, the thread ~95% busy, ~45 us a draw; FXCENSUS ~260 particle quads a frame); 11 frame 77, game-bound (game 76, render 34, idle half). Level 11's steady profile: 0x43D00 the hottest own (6.2%, up to 8.3%), under 0x4D270 -> 0x4C560 (the rain's system, lists at 0x40F848..0x40F884): each new drop's line walked across the ground's height grid until it is under a cell (no length limit). The 0xF8EB3 time (11% in the first block) is 0xE00E0's start-of-level waits (Sleep), not play.
- v228: 0x43D00 native (native_hot.c tt_nat_00043D00, recomp_0004.c's renamed _gen; TT_NATIVE_GRID=0: lifted): the grid walk on host floats/doubles with the lifted code's exact conversions (x87 doubles, float stores, truncating fist with the guest control word), compares, eax/ecx/edx, x87 compare state, the argument slot it overwrites with its step, g_ebp/g_seh_ebp. Host-checked (work/nt run_43d: 30000 cases, 15337 hits / 14538 leaving the grid, 0 differ in registers, x87, memory). Note: the guest-register macros (eax, ebx, edi...) clash with local names in native code -- name locals r_a etc.
- v229 (the user: remove the rain everywhere and the effects that cost frames): TT_RAIN=0 default -- the weather system's step 0x4D270 (F2FF0, only in weather levels; cdecl, answer unused; recomp_0005.c's renamed _gen) and the drops' line draw 0x4BC20 not run (lists stay empty); TT_TRAILS=0 default -- the weapons' swing trails 0x24C80 not run (cdecl, answer unused); TT_FARFX=0 default -- particle quads (0x48CD0) 2.5+ camera-to-hero lengths from the camera not built (FXCENSUS bins 4/5). Each =1 restores it. v228's native grid walk kept (TT_RAIN=1).
- v229 log: no gain seen (Amon Hen frame 63, Helm's Deep 1 game 66 -- another stretch; its profile as v227's, the rain was ~1.5% there: the 43D00 cost was level 11's), level 11 not reached: a crash at Helm's Deep 2's start (after its film) in 0x1181A0 (DrawPrimitiveUP) from 0x48F30: every quad of a bucket skipped by TT_FARFX, so a draw of 0 quads. v230: a far quad skipped only when the bucket already holds one ([0x407E30] > 0).
- v230 log (Helm's Deep 3, a capture of a crowded battle): level 11 59 ms (77 in v227: the native grid walk then no rain), game-bound (game 58, render 33); level 10 75 ms, render-bound (1685 draws, render 71: vitaGL+GXM ~30% of it, EndScene 10.8%, idle 14%).
- v231 (the user: 1 + 2 + 3): (3) linear textures not seen rewritten checksummed every 4th frame shown (nv2a_sw3d.inc texc_epoch; 1-2 a frame cost ~2 ms; TT_TEXLIN_EVERY=1 every frame). (1) the lights' pass (TT_LIGHTS) dropped in gpu_batch before the targets / raster / program setup (only ONE/ONE draws look the program up twice). (2) THE GL WORKER THREAD (src/video/vgl_glt.c + vgl_glt_gen.inc from tools/glt/gen_glt.py; 95 vitaGL entry points --wrap'd in twotowers/CMakeLists.txt): the render thread queues every vitaGL call to a thread of its own (priority 160, cores 1/2: below the sound), 8 MB ring; asynchronous calls carry their arguments and copies of pointed-to data (uniforms, deleted names), synchronous ones (answers, pointers read later: gen/create/get, shaders, texture uploads, allocators, readbacks, glFinish) wait for the worker. Draws: the render thread keeps a shadow of the vertex arrays (VAO, attribute enables/pointers, array/element bindings); client arrays (0..top) and client indices copied into the queue at the draw, the worker points the attributes at the copies and back. vitaGL state the renderer touches itself goes through the queue: the fake buffer objects' memory word (glt_store32), attributes through them (glt_va_pointer_unit: vertex_array_unit swapped on the worker), v170's span copy (glt_dz_copy: copied from guest memory into the queue on the render thread, into vitaGL's pool on the worker), the depth-surface keep (glt_call). The swap waits for the frame before (one frame at most in the queue); vglGetFrameNumber = the worker's swaps. Started at the 30th present; drained and off while a capture is made. [GLT] every 10 s; [RTPROF] also shows the worker's zones ("glt: ..."). TT_GLT=0: off; TT_GLT_PRIO=<n>. Host-checked (work/glt: mock vitaGL, 300 frames of random draws -- client/interleaved/own-buffer/stack arrays, ring wrap, sync calls -- the same calls, vertices, indices and uniforms reach vitaGL with and without the worker).
- v231 log: the GL worker works (no stalls, no broken picture reported). Medians: Amon Hen 44.6 ms (58-63 before: 22 fps), Helm's Deep 3 49.4 (59: 20 fps), Helm's Deep 1 62 (game-bound, game 60), Helm's Deep 2 72 (GPU-bound now: the worker 32% in sceGxmEndScene, 2061 draws). [GLT]: ~1000-3900 commands / 100-260 KB a frame, 1-6 synchronous calls a frame, of them glGetError 1.0 (gpu_present's end-of-frame check): the render thread waited 6-9 ms a frame for that answer (the whole frame drained, "after the present" 9-23% of [RTPROF]).
- v232: that glGetError not asked with the worker on.
- v232 log (500 MHz): Fosso 2 54 ms median (72), Rohan 2 46 (51-56), Fosso 3 61 (a heavier stretch; heaviest frames 75-88 ms, the game's code alone). The heavy Fosso 3 frames' game thread (PROFINC): 0xF2FF0 ~50% of which the characters' updates 0xC79B0 20-26% (0xB6550 -> 0x5F130 the pathfinder on the occupancy grid 8%, 0x5E930 its recursive search, 0xB9530 collisions 6-7%, 0x843F0 AI 5%), the draw walk 0xF1310 / 0x522E0 16-20% (of it the poses 0x2D820 ~10%: 0x2CA60 -> per-bone track samplers 0x2BCD0 / 0x18270 / 0x2C640, ~450 bones a frame, ~15 us each), the sound manager 5-7%. The pathfinder's answer depends on the dynamic occupancy (0x5C200/0x5C160 around each call): not memoisable; the poses' tree is ~3000 lifted lines over 8+ functions with fsin/fsqrt.
- v233: recomp_fist / recomp_frndint (templates/runtime/recomp_types.h and the gen copy) without libm calls for |v| < 2^31 (32-bit fist) / 2^52 (frndint): one VFP conversion and an adjustment instead of floor/ceil/trunc/fmod/ldexp (1471 libm call sites in the lifted code). Host-checked: 60 million values (random bits, halves, the limits, +-0, NaN/inf) x 4 rounding modes x 16/32/64 bits, identical to the old.
- v231-v233 logs: [GPUWAIT] "the frame wait had lost its signal" 10-26 times a session (2-12 s each: freezes) -- new with the GL worker: the render thread now runs ahead, the frame-start callback (0xDFFA0 0) lands right after the last frame's end (1) before the game's 0xE00E0 looks at 0x4B7601, and the game waits for an end it has not sent. v233's medians: Fosso 3 43.4 ms (23 fps), Rohan 2 49.5 (heaviest stretches render-bound: 3600-3960 draws, render 105-115 ms), Fosso 2 60.6.
- v234: v159's frame counting (TT_GPU_AHEAD) on by default: frames the game finished vs frame ends the GPU ran -- with the GPU inside the game's very frame, the game goes on. TT_GPU_AHEAD=0: the stock wait.
- v235 (the user: everything that gains fps): lever 1/2 studied first -- the generated code's remaining global change (a RAM fast path in XBOX_PTR) would void the PGO profile (~5%) for at most a few %, and the top own functions left are 1-1.3% each (the hot ones already native); not done. Instead: (1) XACT's DoWork worker (v190) on again by default, only in play (tt_in_play; menus/films/scenes keep it on the game thread), priority 150 on core 2 (the sound chip now ~28% busy, not v192's 85%); TT_XACTW=0 off. (2) pose LOD for figures posed without a world matrix (half the poses in the v234 [ANIMLOD]: never kept before): their owner's place [+0x11C]+0xF0, never the heroes'; TT_ANIMLOD_OWNER=0 off. (3) [BONES] census off unless TT_BONECENSUS=1 (a walk over every pose's bones). (4) libvitaGL rebuilt with HAVE_VERTEX_LAYOUT_CACHE=1 only (not v210's SKIP_ERROR_HANDLING: null checks the renderer may lean on); /home/claude/vgl-src/lc, stock kept as libvitaGL.a.stock.
- v235 log (Helm's Deep 3, level 11): 53.4 ms median (59.0 in v234), the user: much smoother; [XACTW] ~0.8-3 ms a run, longest 31. A crash right after the level: free() inside bridge_NtClose (the game closing the level's sound files, 0xEF450 -> 0xEAA90 -> 0xF95E9) -- the XACT worker streaming from a wave bank's handle the game closed (use after free of the native object). v236: bridge_NtClose first calls xbox_close_wait_hook (weak in kernel_bridge.c; twotowers_manual.c waits, at most 200 ms, while the worker is inside a run, unless the closer is the worker); the worker skips a request left over once play is over; the handle table's slot allocation under a small lock (two threads opening at once could take one slot).
- v236: a crash as a level was chosen (before any play, so the XACT worker not yet started): free() on the GL worker inside vitaGL's shader compile (shark) -- heap damage again, the other one new in v235 being the layout-cache libvitaGL (v235's crash at the level's end, also in free(), likely the same). v237: the stock libvitaGL back (the layout-cache build kept in /home/claude/vgl-src/lc); v236's close wait and handle lock kept. 16:9 the default again (the user asked; main.c and gfx_menu.c: TT_WIDE=0 for 4:3; TT_RES 0.75 = 640x360 when wide).
- v237 log (16:9, Amon Hen / Helm's Deep 1 / 2): medians 51.0 / 63.5 / 54.4 ms (the user: much better, no crash; some in-game sound out of step). [XACTW] runs 1.1-3.6 ms on average but up to 70-82 ms late now and then. v238: the worker at priority 135 (above the sound chip's 140, which is ~27% busy) so a run is not put aside behind it.
- v239-pgo: a new PGO profile run (TT_PGO=gen in build-pgogen, v238 sources): the v194 profile predates every native function and wrapper since. The user plays 15-20 min (Amon Hen, Helm's Deep 1-3) and sends ux0:data/twotowers/pgo; then TT_PGO=use in build-pgogen again with the new .gcda in TT_PGO_DIR (the old ones kept in /home/claude/pgo-v194).
- The pushbuffer read off the game thread (proposed to the user) dropped after the v237 [PERF]: the game thread spends 1.4-1.7 ms of a 45-48 ms frame reading GPU commands (3%), and the render thread is already at 37-44 ms: nothing to win, a hang to risk.
- v239-pgo run: one session, 6 dumps (18 min): Amon Hen, Helm's Deep 1 whole, Rohan 2, Helm's Deep 2/3 a little. 38 .gcda in /home/claude/pgo240 (v194's in /home/claude/pgo-v194). [XACTW] at 135: longest run per 10 s median 15 ms, p90 38 (instrumented) -- the long runs are XACT's own work, not the priority.
- v240: TT_PGO=use with /home/claude/pgo240 (build-pgogen reconfigured; TT_PGO_DIR in its cache). .text 15.65 MB.
- v241: (1) BlockUntilNotBusy (0x0011AC90) wrapped (the gen as sub_0011AC90_gen): v240's profile had 5.8% of the game thread in it at Helm's Deep 3. Called from BeginPushBuffer (return 0x0011AFDB) the resource is a pushbuffer about to be recorded again; a pushbuffer's words are copied into the render thread's ring as they are kicked, so a kick (0x0011A740) replaces the wait (not while D3D records, nor with the GPU off: device flags 4/0x2000; not when set on the device: 0x780000). TT_PBNOWAIT=0: D3D's wait. [BUNB] every 10 s: the remaining waits by caller (pushbuffers / vertex-buffer Lock 0x001191B0 / others) and their ms. (2) The read-ahead's fetch thread takes every request, oldest first (v157-v240: one at a time, any asked while one waited dropped -- with two or three streams a stream's next piece often never fetched, the 20-100 ms stalls in v240's [READS]); 10 slots instead of 6. (3) Internal resolution: 16:9 608x342 (TT_RES 0.7125; was 640x360), 4:3 512x384 (0.8; was 544x408).
