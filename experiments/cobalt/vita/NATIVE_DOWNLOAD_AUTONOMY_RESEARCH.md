# YouTubeVita: download autônomo (sem Cobalt Tools externo)

Investigação de arquitetura — 18/11/2026. Nenhum código executável alterado nesta etapa.

## Requisito de produto

Vídeo aberto no YouTube TV -> Triângulo -> Baixar -> salvar o vídeo inteiro
em ux0:data/youtubevita/downloads/ com formato e qualidade já escolhidos
em Add-ons > Configurações, sem pedir URL, API, chave ou PC.

## Evidências verificadas

1. Atual SbPlayer em port_player.cpp recebe apenas samples de vídeo H.264
   e áudio AAC, timestamps, dimensões, decodificadores e callbacks. O struct
   SbPlayerCreationParam (Starboard API 15) não inclui videoId, canal ou URL.
2. Cobalt Evergreen 5.50.2 (25.lts.50, ARM release) é carregado como
   libcobalt.so pré-compilado. O videoId pertence ao JS do aplicativo
   YouTube TV, não à API Starboard de reprodução; não foi encontrada
   uma ponte JS -> native que entregue o videoId.
3. O Cobalt oferece DevTools em builds NON-GOLD, via porta 9222 e console
   JavaScript, mas a biblioteca oficial incluída no app é RELEASE.
   Não assumir que DevTools funciona ou é seguro no dispositivo.
   Doc: https://developers.google.com/youtube/cobalt/docs/gen/cobalt/doc/web_debugging
4. third_party/vitube possui resolvedor nativo de vídeos por Innertube/
   HLS (VISIONOS, ANDROID, TVHTML5), sem servidor externo. É material
   de referência para criar um backend local, sujeito a mudanças na
   API não pública do YouTube. Respeitar a licença GPL-3.0-only e
   a compatibilidade de licenças ao incorporar código.
5. Cobalt Tools é um serviço Node/API externo; api.cobalt.tools não
   é uma API pública livre para terceiros (captcha/bot protection).
   Empacotar o JS do servidor sem mais infraestrutura não o torna
   um serviço nativo autônomo no Vita.
6. Salvar os frames e os samples AAC que já passam pelo SbPlayer
   equivale a *gravar* uma reprodução a partir do ponto observado,
   não baixar o vídeo completo, não oferece controle real de resolução,
   e ainda exige muxer TS/MP4. Não apresentar isso como download direto.

## Caminho técnico correto

A) Extrair e validar o videoId EXATO do JS da página atual por um
   mecanismo de comunicação deliberado e seguro, sem acessar cookies
   ou interceptar TLS de forma insegura. O ID precisa ser invalidado
   quando o player mudar e não pode ser inferido de miniatura/título.

B) Resolvido o ID, consultar diretamente o backend de mídia do YouTube
   no Vita com TLS seguro, reutilizando seletivamente a arquitetura de
   ViTube e tratando mudanças/assinaturas/cifras com falha segura.

C) Obter stream H.264 e AAC compatíveis com Vita, transferir com thread
   dedicada e poucos buffers, e montar em MP4 para armazenamento
   (sem esgotar os 365 MiB de RAM). Formatos/qualidades via addons.cfg.

D) Criar testes: vídeo completo, busca/playlist/mudança rápida de vídeo,
   360p/480p/720p e áudio, erro HTTP, economia de memória,
   cancelamento, gravação, integridade, bateria/suspensão.

## Estado do projeto após esta investigação

A v0.41 aceita configuração de instância Cobalt Tools pelo menu, porém
NÃO baixa o vídeo atual automaticamente. Nada neste documento afirma que
download autônomo funciona. Não renomear botão ou liberar como uma nova
release até resolver A+B+C e testar.
