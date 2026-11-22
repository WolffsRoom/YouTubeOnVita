# Estrutura do projeto (organizado em 22/11/2026)

| Pasta | Conteúdo |
|:--|:--|
| `experiments/cobalt/vita/` | Código do port (Starboard, player, Add-ons, loader) |
| `tools/` | Scripts em uso: `build_v065.ps1` (build atual), `build_youtube.ps1`, `build_starboard.ps1`, `test_cobalt_boot_vita3k.ps1` |
| `tools/legacy-builds/` | Scripts de build das versões v0.59 a v0.64 (ajustados para rodar desta pasta) |
| `tools/vita3k/` | Configs avulsas do Vita3K e `capture_check.py` (antes soltos na raiz) |
| `tools/capture/` | Capturas dos testes no Vita3K (recriada a cada teste; as antigas foram apagadas em 22/11/2026) |
| `build-youtube-v065-*` | Build atual (Full e Lite), usada pelos testes no Vita3K |
| `output/v058/` | Release pública v0.58 |
| `output/v064/` | Pacote de release v0.64 + `README_VALIDACAO.txt` |
| `output/v065/` | Build atual v0.65; `output/v065/release/` = arquivos para a release |
| `archives/` | Backups de código antes de cada mudança (`*-prepatch`), análises de crash. VPKs, `libcobalt.so` e cópias do Vita3K antigas foram apagadas em 22/11/2026 |
| `archives/crash-dump-notes/` | Notas `note_*.bin` extraídas de um psp2dmp (antes na raiz) |
| `assets/livearea/` | Bolha da LiveArea; `old-versions/` = versões antigas (antes em `assets/Old`) |
| `assets/brand/` | Recursos oficiais da marca YouTube |
| `assets/promo/` | Capa, post de release, GIFs, PSDs de divulgação |
| `assets/reference/` | Imagens de referência |
| `assets/UI Sounds/`, `assets/Start-up YouTube.mp4` | Fontes de som e da abertura (ficaram no lugar: scripts apontam para eles) |
| `assets/prints/`, `assets/sony-buttons/` | Imagens usadas no README |
| `logs-psv/` | Logs do Vita real (fora do Git) |
| `current/` | Pasta da release atual (não alterada) |
| `docs/`, `legacy/`, `third_party/` | Documentação, protótipo antigo, dependências |
