# AnyTV — estado da entrega e o gesto que falta

## O que está pronto e verificado aqui

| Item | Estado | Prova |
|---|---|---|
| Núcleo ADB em C (CNXN/AUTH, `shell` v2, `sync`) | funciona contra adbd real | conectou no Android 12 do homelab (`redroid`), rodou `id`, leu `getprop` |
| Parser de `shell,v2` (id + tamanho u32 + dados) | correto | frames reais do device: `01 ed000000 uid=2000(shell)…`, `03 01000000 00` (exit 0) |
| RSA-2048 próprio (keygen + PKCS#1 v1.5 SHA-1) | verificado independente | OpenSSL e Python confirmam `sig^e mod n` = DigestInfo+SHA1(token); `n0inv·n ≡ -1 mod 2^32`; `rr = R² mod n` |
| Blob de chave pública 524 bytes (formato do adbd) | idêntico ao spec do AOSP | chave do app = chave gravada; Python valida a assinatura do fio contra ela |
| Assinatura aceita pelo adbd do redroid | **não comprovado** | adbd (imagem Magisk) recusa até com par de chaves gerado pelo OpenSSL; CNXN sem auth e todo o resto funciona |
| App SwiftUI compilando | **não comprovado** | não existe Mac aqui; o CI é quem compila |
| IPA gerado | **não gerado** | depende do CI (GitHub Actions `macos-15`) |

## O gesto que falta (2 minutos, no navegador)

1. Criar repositório **vazio** em https://github.com/new — nome `anytv`, **sem** README, sem .gitignore, sem licença.
2. Em `https://github.com/sfsantos04/anytv/settings/keys` → **Add deploy key**, colar a linha de
   `deploy_key.pub` (está nesta pasta) e marcar **Allow write access**.

Depois disso é só responder "pode subir": eu faço o push, acompanho o run, baixo o IPA
do release, confiro sha256 + Info.plist + Mach-O arm64 + símbolos do app e te entrego o
arquivo com o caminho.

## Se quiser compilar sem mim

```
cd ios && xcodegen generate
xcodebuild -project AnyTV.xcodeproj -scheme AnyTV -configuration Release \
  -sdk iphoneos -destination 'generic/platform=iOS' CODE_SIGNING_ALLOWED=NO build
```

Ou com Mac + Xcode: abrir `ios/AnyTV.xcodeproj`, Time, rodar no iPhone.

## Instalar

IPA sem assinatura → SideStore/AltStore (grátis expira em 7 dias, pago 1 ano),
ESign/Feather no próprio iPhone, ou Sideloadly no Windows.

## Preparar a TV (uma vez)

Ajustes > Sobre > 7 toques em "Compilação" > Opções do desenvolvedor >
**Depuração USB** e **Depuração de rede** ligadas. A TV precisa estar na mesma
rede do iPhone. Na primeira conexão a TV pede autorização — aceite com o
controle dela (o app espera até 90 s).
