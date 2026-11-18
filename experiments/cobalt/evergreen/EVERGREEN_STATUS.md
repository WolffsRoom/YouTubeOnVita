# YouTube Vita — Evergreen ARM port viability (2026-11-16)

## Authentic component, not a visual mock

Official Cobalt Evergreen 5.50.2, release `25.lts.50`,
`cobalt_evergreen_5.50.2_arm-hardfp_sbversion-15_release.crx`.
SHA-256: `be5b93dc4dcd42bd8dea5bfda728e6a170a17d6eef913b2cf78d370ac5e011dd` (verified against GitHub release API).

Extracted `lib/libcobalt.so`: 33,491,716 bytes, ELF32 little-endian ARM EABI5 hard-float (VFPv3), four PT_LOAD segments; virtual extent 33,806,836 bytes. None of this is the Nintendo Switch executable; it is the Google-provided runtime engine.

### Real Vita3K results

1. `YTVSB0001` (StarboardVitaTest.vpk): 17/17 native API tests passed — log `ux0:data/youtubevita/starboard15.log`.
2. `YTVEL0001` (CobaltEvergreenInspect.vpk): 18/18 read-only ELF tests passed — log `ux0:data/youtubevita/evergreen_inspect.log`.
3. Vita3K can open all 33.49MB from app0 and read/copy it into one contiguous 34MB Vita user RW memory block, then release that block.

**The DLL was never executed.** The inspector only checks validity and memory allocation; it is not an ELF loader, browser, YouTube client, or demonstration of a 1:1 UI.

### Remaining native integration

Google's library has 209 undefined dynamic symbols: 197 `Sb*` APIs and 12 `kSb*` constants. The current Vita port exports only 12 of these 209 symbols; 197 remain missing. The exact list and per-domain grouping are in `runtime_gap.json`.

Relocation entries:
- `R_ARM_RELATIVE`: 61,413
- `R_ARM_ABS32`: 2,884
- `R_ARM_JUMP_SLOT`: 664
- `R_ARM_GLOB_DAT`: 157
- Total: 65,118

The upstream Cobalt source includes `starboard/elf_loader`, which requires executable mapping. The Vita ABI's `SB_CAN_MAP_EXECUTABLE_MEMORY` remains **0** because executable memory mapping / W^X permission changes are not implemented or tested yet. Allocating **RW** memory does **not** prove that code can execute.

No ELF relocation was applied in guest memory. Actual run requires correct memory permissions and API function table, relocation application, and load/initialization routines. Missing categories include GXM/EGL/GLES display backend, player/audio, Starboard file/net/thread primitives, and service dependencies.

### GN build alternative

The source checkout has experimental `vita-armv7` GN platform registered; `gn gen` reached actual graph traversal but stopped at missing `//third_party/crashpad/crashpad/wrapper/BUILD.gn`. Cobalt 25 whole-runtime compilation requires a complete dependency sync and a viable Vita graphics/OS port. No Cobalt browser binary compiled from source yet.

### Fundamental product limitation

Even a functioning Cobalt runtime does not prove the proprietary YouTube-on-TV app will be served to an uncertified Vita device, nor that login and every video playback will work. The 1:1 target must be validated against real YouTube UI and native console controls at every step. We cannot label this 1:1 until it is genuinely displayed.

### Next steps

1. Implement and test executable memory layout/permissions in Vita user context without executing unverified code.
2. Compile selected upstream ELF loader code, validate relocation families and correct export resolution with **known safe test ELF**.
3. Implement real Starboard system, file, threads, sockets and memory contract; do not substitute no-op stubs for vital APIs.
4. Implement real GXM-compatible interface capable of running the Cobalt rendering path, then audio/video, then official frontend.
5. Only then attempt the live YouTube-on-TV app, with accurate diagnostics for certificate/auth/certification constraints.

 
## Atualização de 16/11/2026 — validação da memória executável + resolução parcial

Com kubridge instalado no Vita3K, a reserva de 34 MiB recebeu os quatro segmentos do Cobalt e as 64.907 realocações internas. Depois disso foram resolvidas 12 importações Starboard para funções nativas realmente existentes no projeto. Restam 199 referências externas não resolvidas. O segmento de texto oficial (24.674.304 bytes) foi protegido RX, mantendo a BSS RW; uma função diagnóstica confiável retornou 42 em uma página reservada. O código do Cobalt NÃO foi chamado.

Teste YTVRL0001: 24 verificações PASS, 0 FAIL. Executar de novo com tools/test_relocations_vita3k.ps1.
 
## Novas APIs Starboard validadas (16/11/2026)

Arquivos e diretórios do Vita (ux0, sandbox) + pthread mutex/condition completos nos testes. O import_lookup agora oferece 38 exportações reais do Starboard 15 ao Evergreen (173 referências externas continuam sem resolver). VPK YTVSB0001: 50 PASS, 0 FAIL; VPK YTVRL0001: teste comprovado de 24 PASS após ampliar para 38 imports, mas repetição posterior incompleta. Rede SceNet nativa: 6 PASS no teste isolado inicial; repetição automática inconclusiva. O aplicativo oficial do YouTube ainda não foi executado.
 
## Actual H264 playback route discovered (17/11/2026)
The Vita-native SceAvPlayer API decoded 60 real YUV H264 frames from a newly generated original MP4 at 960x544, with 59 distinct frame-to-frame hashes, on Vita3K; 6 PASS, 0 FAIL. The direct sceAvcdecDecode path still fails pixel-verification (14 PASS, 1 FAIL). Builtin VitaSDK FFmpeg software library lacks H264 decoder. Cobalt relocator now links 159 of 211 external references; 52 unresolved. 137 Starboard native API tests passed. Cobalt itself, YouTube DOM/JS, SbPlayer, and video-on-screen are not executed yet. Full logs and media test report are in experiments/cobalt/vita/VIDEO_DECODER_REPORT.md.
