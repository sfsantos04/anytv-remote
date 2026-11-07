# AnyTV - controle remoto de Android TV pelo iPhone

App iOS (SwiftUI) que controla **qualquer** Android TV, caixa Android TV e TV box
AOSP pela rede local, sem depender do servico de controle do Google. O caminho e o
proprio ADB: o app fala o protocolo de host do `adb` (CNXN/AUTH, servico `shell` v2,
`sync` para puxar arquivos) e executa `input keyevent`, `input text`, `am`/`monkey`
e `screencap` na TV.

## O que ele faz

- D-pad, OK, voltar, home, menu, volume, mudo, play/pause, anterior/proxima,
  entrada de TV, info, ajustes, ligar/desligar.
- Teclado: envia texto digitado no iPhone para a TV (`input text`).
- Lista de apps instalados na TV e abertura por toque.
- Espelho da tela da TV (captura `screencap` + `sync`, atualizando a cada 2 s).
- Descoberta por varredura da /24 na porta 5555, ou IP manual.
- Chave RSA-2048 gerada no aparelho e guardada no Keychain; a TV autoriza uma vez.

## Diferenca para o app oficial

O app do Google so funciona em aparelhos certificados com o servico remoto dele.
Este funciona em qualquer build com depuracao ADB ligada - inclusive boxes antigos,
Android 4.x em diante - e ainda da lista de apps e espelho de tela.

## Preparar a TV

1. Ajustes > Sobre > tocar 7x em "Compilacao" (build).
2. Opcoes do desenvolvedor > **Depuracao USB** e **Depuracao de rede / ADB** ligadas.
3. Ver o IP da TV: Ajustes > Rede (ou `Status`).
4. No app: "Procurar na rede" ou "Adicionar por IP".
5. Na primeira conexao a TV mostra "Permitir depuracao USB?" - aceite com o
   controle da propria TV. O app espera ate 90 s por isso.

## Estrutura

```
core/      cliente ADB em C99 puro (protocolo, RSA-2048, SHA-1, servicos, sync)
tests/     testes de cripto e de comandos, rodam sem aparelho
ios/       app SwiftUI + project.yml (XcodeGen)
tools/     utilitarios de laboratorio (docker exec/cp, varredura TCP, chaves)
scripts/   gerador do icone
```

Nada de `core/` depende de Foundation; o mesmo codigo compila no Linux (CLI de
teste) e no app iOS, com a ponte de sockets em `core/bridge_ios.c`.

## Compilar

Sem Mac, o IPA sai do GitHub Actions (`macos-15`) em
`.github/workflows/build.yml` e e publicado como release `build-<n>`.

```
cd ios && xcodegen generate
xcodebuild -project AnyTV.xcodeproj -scheme AnyTV -configuration Release \
  -sdk iphoneos -destination 'generic/platform=iOS' CODE_SIGNING_ALLOWED=NO build
```

## Instalar no iPhone

O IPA nao tem assinatura (o CI nao tem certificado). Assine no aparelho ou no PC:

- **SideStore / AltStore** (conta gratis: expira em 7 dias; paga: 1 ano)
- **ESign / Feather** direto no iPhone
- **Sideloadly** no Windows

## Testes

```
clang -std=c99 -O2 -Wall -Wextra -Werror -DANYTV_HOST_LINUX \
  -o test_crypto tests/test_crypto.c core/anytv_bn.c core/anytv_sha1.c core/anytv_rsa.c
./test_crypto /tmp/vector.json

clang -std=c99 -O2 -Wall -Wextra -Werror -DANYTV_HOST_LINUX \
  -o test_core tests/test_core.c core/anytv_keys.c
./test_core
```

`tests/test_crypto.c` confere SHA-1 contra vetores conhecidos, gera a chave,
assina um token e **auto-verifica** a assinatura PKCS#1; o arquivo de vetores
gerado permite conferir tudo com Python/OpenSSL de forma independente.

## CLI de laboratorio

`core/anytv_cli.c` compila no Linux e exercita o mesmo nucleo do app:

```
anytv-cli <host> <porta> <arquivo-de-chave> info
anytv-cli ... key DPAD_DOWN
anytv-cli ... text "ola tv"
anytv-cli ... apps
anytv-cli ... shot /tmp/tela.png
```

## Notas de protocolo (o que o aparelho exigiu)

- O nome do servico no `A_OPEN` vai **terminado em NUL** (`strlen + 1`).
- `shell,v2,raw:<cmd>` responde em pacotes `id(1 byte) + tamanho(u32 LE) + dados`;
  `id=3` carrega o codigo de saida. Firmware antigo cai no servico `shell:` sem
  codigo de saida - o cliente tenta v2 e depois o legado.
- A chave publica vai no formato proprietario de 524 bytes
  (`modulus_size_words | n0inv | modulus LE | R^2 LE | expoente`), e nao em DER.
- Assinatura: PKCS#1 v1.5 com SHA-1 sobre o token de 20 bytes, big-endian.
- `adb_keys` sem quebra de linha no fim evita a entrada vazia que o adbd tenta
  decodificar.
