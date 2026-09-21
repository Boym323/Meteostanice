# Meteostanice

Firmware hlavní meteostanice pro ESP8266 / Wemos D1 mini Pro.

## Funkce

- 7× DS18B20 pro teploty v různých výškách/hloubkách
- Si7021 pro relativní vlhkost
- BMP180 pro tlak přepočtený na hladinu moře
- MQTT reporting
- Meteotemplate reporting přes HTTPS
- autentizované OTA aktualizace
- pevná IPv4 konfigurace

## První sestavení

Projekt používá PlatformIO.

```bash
cd "Hlavni meteostanice"
cp src/secrets.example.h src/secrets.h
```

V `src/secrets.h` nastav:

- Wi-Fi SSID a heslo
- MQTT uživatele a heslo
- OTA uživatele a silné unikátní heslo
- Meteotemplate API heslo
- aktuální SHA-1 fingerprint TLS certifikátu `pocasi-loucka.cz`

Soubor `src/secrets.h` je v `.gitignore` a nesmí se commitovat.

Pak lze firmware sestavit:

```bash
pio run
```

a nahrát přes USB:

```bash
pio run -t upload
```

## Bezpečnost

Repozitář historicky obsahoval přístupové údaje přímo ve zdrojovém kódu. Je nutné považovat původní Wi-Fi a Meteotemplate údaje za kompromitované a změnit je. Samotné odstranění z aktuálního souboru je neodstraní z Git historie.

OTA endpoint `/update` se nyní aktivuje pouze s nakonfigurovaným uživatelem a heslem.

Meteotemplate upload používá HTTPS s explicitním fingerprintem certifikátu. Pokud fingerprint není nakonfigurován nebo se certifikát serveru změní, upload se bezpečně odmítne místo vypnutí ověřování TLS.

## Chování při chybách senzorů

Neplatné měření se už nepovažuje za nové platné měření. MQTT obsahuje objekt `valid`, který říká, které hodnoty v daném cyklu prošly validací. Meteotemplate parametry pro neplatná čidla nejsou odeslány.

## Síť

Výchozí síťové parametry zůstávají kompatibilní s původním zapojením:

- ESP: `192.168.1.31`
- gateway/DNS: `192.168.1.1`
- MQTT: `192.168.1.2:1883`
- mDNS: `mainstation.local`

Pokud je potřeba jiná síť, uprav konfiguraci před nasazením.
