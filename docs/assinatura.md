# Assinatura dos APKs

O app do Gtavport (`com.gtavport.app`) é assinado sempre com a mesma chave, para que cada release nova
instale **por cima** da anterior sem desinstalar. Desinstalar apagaria os dados do jogo instalados no app.

Certificado (SHA-256), para conferir um APK baixado:

```
6A:F6:4F:D0:90:63:B3:D0:F7:52:27:49:68:46:3A:E0:D9:EA:32:B0:8C:DE:E6:59:15:C9:1B:4A:E7:A6:A5:1F
```

```sh
apksigner verify --print-certs gtavport-vX.apk   # "certificate SHA-256 digest" tem de bater (sem os ':')
```

A chave privada **não** está neste repositório. Ela fica com o mantenedor e nos secrets do workflow
(`ODIN2_KEYSTORE_B64`, `ODIN2_KS_PASS`, `ODIN2_KEY_ALIAS`, `ODIN2_KEY_PASS`). O workflow de release
confere que o APK saiu com este certificado e não publica com outra chave, a menos que isso seja pedido
explicitamente (`allow_temp_key`).
