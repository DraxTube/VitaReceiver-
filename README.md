# VitaReceiver 📺

**DLNA Media Renderer for PS Vita** — Ricevi e riproduci video dal tuo telefono direttamente sulla PS Vita!

## Come funziona

VitaReceiver trasforma la tua PS Vita in un ricevitore DLNA. Usa **Web Video Caster** (o qualsiasi app DLNA controller) sul telefono per inviare video alla Vita.

```
📱 Web Video Caster  ──WiFi──▶  🎮 PS Vita (VitaReceiver)
     (controller)                    (renderer)
```

## Requisiti

- PS Vita con **HENkaku/Enso** (firmware custom)
- **VitaShell** per installare il VPK
- **WiFi** — telefono e Vita devono essere sulla stessa rete
- **Web Video Caster** (Android/iOS) o qualsiasi app DLNA controller

## Installazione

1. Scarica l'ultimo `VitaReceiver.vpk` dalla pagina [Releases](../../releases)
2. Trasferiscilo sulla Vita via USB o FTP
3. Apri **VitaShell** e installa il VPK

## Utilizzo

1. **Avvia VitaReceiver** sulla PS Vita
2. Sullo schermo vedrai l'indirizzo IP e "In attesa di connessione..."
3. **Apri Web Video Caster** sul telefono
4. Tocca l'icona di cast e seleziona **"VitaReceiver"** dalla lista
5. Scegli un video da riprodurre — verrà trasmesso alla Vita!

### Controlli sulla Vita

| Tasto | Azione |
|---|---|
| △ Triangle | Pausa / Riprendi |
| ○ Circle | Stop / Chiudi errore |
| ✕ Cross | Mostra barra progresso |
| START (tieni premuto) | Esci dall'app |

## Formati supportati

- **Video HTTP/HTTPS**: MP4 (H.264) — riproduzione diretta via streaming
- **HLS (m3u8)**: Stream live e VOD — scarica e riproduce i segmenti

> **Nota**: La qualità massima dipende dall'hardware della Vita. Per HLS, viene selezionata automaticamente la qualità migliore fino a 720p.

## Build da sorgente

### Con GitHub Actions (consigliato)

1. Fai fork di questo repository
2. Vai su **Actions** → **Build VitaReceiver**
3. Clicca **Run workflow**
4. Scarica il VPK dagli artifacts

### Locale (richiede VitaSDK)

```bash
export VITASDK=/usr/local/vitasdk
mkdir build && cd build
cmake ..
make
```

Il file `VitaReceiver.vpk` verrà generato nella cartella `build/`.

## Architettura

```
┌─────────────────────────────────────────────┐
│                 main.c                       │
│           (State Machine + Loop)             │
├──────────┬──────────┬──────────┬────────────┤
│ ssdp.c   │ upnp_    │ player.c │   ui.c     │
│ (SSDP    │ server.c │ (sceAv   │ (vita2d    │
│ discovery│ (HTTP +  │  Player  │  status)   │
│  UDP)    │  SOAP)   │  + HTTP  │            │
│          │          │  I/O)    │            │
├──────────┴──────────┤          ├────────────┤
│   soap_handler.c    │  hls.c   │            │
│  (AVTransport +     │ (m3u8    │            │
│   ConnManager)      │  parser) │            │
├─────────────────────┴──────────┴────────────┤
│              network.c                       │
│         (WiFi + Socket init)                 │
└─────────────────────────────────────────────┘
```

## Licenza

MIT License
