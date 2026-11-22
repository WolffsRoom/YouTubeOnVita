# YouTubeVita v0.41 — Configuração nativa de servidor para downloads

18/11/2026 — build EXPERIMENTAL.

## Mudanças implementadas

- Add-ons > Configurações > Servidor Cobalt: abre teclado nativo Vita IME e grava a URL da instância HTTPS própria/autorizada. Usuário não precisa criar download.cfg manualmente.
- Add-ons > Configurações > Chave Cobalt: permite cadastrar chave opcional; nunca exibe a chave existente.
- Qualidade (360,480,720p) e formato (MP4/MP3) continuam persistidos pelo menu e passam para as requisições Cobalt Tools.
- Input físico não é enviado ao YouTube durante a exibição do teclado, evitando ações acidentais.
- Painel de 9 linhas, buffer 520 x 520, render em 520 x 504, na tela Vita de 960 x 544.
- O aplicativo gera o arquivo de configuração no storage do Vita e não exige editar arquivo manual. A chave é armazenada em texto simples localmente; não é gravada nos logs.
- Os estados exibidos agora são Set Cobalt server e Video ID unavailable, em vez de pedir configuração manual.
- Preservados menus de contexto, tela, desempenho, AdBlock parcial e fix stack v0.40.

## Testes

- VitaSDK Docker: todos os componentes C++ necessários, VPK 00.41: OK.
- Vita3K: inicialização e vídeo funcionando, tools/capture/cobalt-boot-20261118-213445.
- Vita3K: Select -> Configurações -> Servidor Cobalt; diálogo IME inicializado com r=0 e YTV_DOWNLOAD_SERVER_IME_OPEN, sem crash durante a simulação; tools/capture/cobalt-boot-20261118-213608.
- Confirmação da digitação e persistência pelo teclado físico ainda deve ser testada no Vita.

## Limitação principal: ainda sem ID automático do vídeo

O Cobalt Evergreen 25.lts.50 recebe o videoId somente dentro do JavaScript da interface YouTube TV, enquanto a API Starboard usada pelo porte recebe frames/codec, sem metadados contendo o videoId. A biblioteca libcobalt.so atual é pré-compilada; não foi implementada ainda uma ponte JS -> C++ nativo (requer patch ou rebuild compatível e testes na plataforma). Por isso, o botão de baixar o vídeo atual com um toque ainda NÃO está funcional. O sistema legado de download_request.txt permanece como alternativa de teste. NÃO anunciar essa build como download automático.

Uma instância Cobalt Tools autorizada continua necessária. Não há servidor oficial aberto para integração arbitrária sem autenticação.

O AdBlock continua PARCIAL: filtro DNS de alguns domínios de anúncios, não remove ads embutidos nos fluxos da própria infraestrutura YouTube; um patch específico em respostas da aplicação seria necessário.

## Artefato

output/native-ui-v041/YouTubeVita-v0.41-NativeDownloadSettings-EXPERIMENTAL.vpk

VPK atual v0.39, protótipo v0.40 e backups não foram substituídos.
