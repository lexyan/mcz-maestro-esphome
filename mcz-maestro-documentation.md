# Poêle MCZ Maestro : modules WiFi, protocole et firmware ESPHome

*[English version](mcz-maestro-documentation.en.md)*

Ce document résume l'analyse des firmwares des modules WiFi d'un poêle à pellets MCZ (technologie Maestro) et leur remplacement par ESPHome pour un pilotage direct depuis Home Assistant.

Il couvre :

1. l'architecture matérielle et les échanges entre les éléments ;
2. le résultat de la décompilation des trois firmwares d'origine ;
3. le protocole série de la carte mère ;
4. les connexions pour reprogrammer les ESP8266 ;
5. la reprogrammation avec esptool ;
6. la configuration ESPHome.

> **Avertissement.** Ces informations viennent d'une analyse de firmwares et d'un projet communautaire, pas d'une documentation du fabricant. Un poêle à pellets est un appareil de combustion : fais les premiers essais de commande à distance en étant devant le poêle.

---

## 1. Vue d'ensemble

### Les éléments

| Élément | Puce | Firmware d'origine | Rôle |
|---|---|---|---|
| Module WiFi 1 (local) | ESP-WROOM-02 (ESP8266, 2 Mo) | Fluidsoft 1.2.4 | Point d'accès WiFi du poêle et serveur websocket |
| Module WiFi 2 (cloud) | ESP-WROOM-02 (ESP8266, 2 Mo) | Fluidsoft « MCZ-RemoteService » 1.2.5 | Connexion au WiFi de la maison et au serveur distant |
| Sonde d'ambiance déportée | ESP8266, 2 Mo | Fluidsoft « MCZ-Sensor » 1.3.1 | Mesure la température de la pièce et l'envoie au poêle |
| Carte mère du poêle | — | 1.8.2 sur le poêle étudié | Interprète toutes les commandes |

Les deux modules WiFi sont deux ESP8266 distincts, montés sur la même carte, chacun relié à la carte mère par sa propre liaison série. La carte mère commande aussi leur mise sous tension, l'un après l'autre (voir la partie 4).

### Qui parle à qui

```
Application (WiFi du poêle) ─┐
                             ├─ websocket :81 ─► Module WiFi 1 ─ série ─┐
Sonde d'ambiance ────────────┘                                          ├─► Carte mère
                                                                        │
Serveur distant ◄─ socket.io ─► Module WiFi 2 ───────────── série ──────┘
```

Point essentiel : **les modules WiFi ne font que relayer**. Aucun des trois firmwares n'interprète le contenu des commandes. Tout le sens du protocole est dans la carte mère.

### Outils utilisés

- Découpage des dumps : script `esp8266_split.py`.
- Décompilation : Ghidra 11.3 (processeur Xtensa), après conversion de l'image applicative en ELF avec les symboles de la ROM ESP8266.
- Les trois firmwares sont construits avec le core Arduino ESP8266 2.3.0 (SDK 1.5.3).

### Organisation de la flash (2 Mo)

| Adresse | Contenu |
|---|---|
| `0x000000` | Bootloader (eboot du core Arduino) |
| `0x001000` | Application |
| `0x1FB000` | EEPROM de l'application (sonde et module cloud) |
| `0x1FC000` | Données de calibration RF |
| `0x1FD000` – `0x1FF000` | Configuration WiFi du SDK |

Les dumps contiennent aussi des restes du firmware AT Espressif d'usine, inutilisés.

> **Confidentialité.** Le dump du module cloud contient le nom et le mot de passe du WiFi de la maison, en clair dans l'EEPROM. Ne partage pas ce fichier tel quel.

---

## 2. Résultats de la décompilation

### 2.1 Sonde d'ambiance « MCZ-Sensor » 1.3.1

**Rôle.** Sonde sur pile qui se réveille périodiquement, mesure la température, l'envoie au poêle, puis se rendort.

**Matériel.**

| Broche | Usage |
|---|---|
| GPIO4 / GPIO5 | I2C (SDA / SCL) vers le capteur NCT75, adresse `0x48` |
| GPIO14 | Alimentation du capteur, activée seulement pendant la mesure |
| GPIO13 | Bouton de réinitialisation (actif à la masse au démarrage) |
| GPIO2 | Sortie configurée mais jamais pilotée |

**Déroulement d'un cycle.**

1. Alimentation du NCT75, lecture de 2 octets, conversion en °C (0,0625 °C par pas), coupure de l'alimentation. Une valeur négative ou supérieure à 100 °C est remplacée par 0.
2. Connexion au point d'accès du poêle avec l'IP fixe `192.168.120.51`.
3. Ouverture d'une websocket vers `192.168.120.1`, port 81, sous-protocole `arduino`.
4. Envoi d'une trame texte unique (voir ci-dessous).
5. Réception de la réponse : deux caractères hexadécimaux donnant le nombre de minutes avant le prochain réveil.
6. Mise en veille profonde. Sans réponse sous 5 secondes, veille avec le dernier intervalle connu (15 minutes par défaut).

**Trame envoyée.**

```
C|RecuperaTemperaturaWiFi|<sonde>|<T>|<version>|<champ5>|<qualité>
```

| Champ | Valeur | Origine |
|---|---|---|
| `<sonde>` | `51`, `52` ou `53` | Numéro de la sonde : `51` = sonde 1, `52` = sonde 2, `53` = sonde 3. Le firmware étudié envoie `51`, codé en dur |
| `<T>` | température × 2, entier | 21,5 °C donne `43` |
| `<version>` | `1.3.1` | Version du firmware de la sonde |
| `<champ5>` | `00` | Jamais modifié ; rôle inconnu |
| `<qualité>` | 0 à 100 | 0 si RSSI ≤ -100 dBm, 100 si ≥ -50 dBm, sinon `2 × (RSSI + 100)` |

La carte mère ne vérifie pas le numéro de version (constaté à l'usage). Elle n'exploite que des demi-degrés.

**Appairage.**

- Sans réseau enregistré, la sonde ouvre un point d'accès « MCZ-Sensor » avec une page « Select your Stove » pendant 8 minutes.
- Au premier contact après appairage, elle envoie en plus `C|WriteBancaDati|356|1|FF`, probablement pour activer la sonde WiFi côté poêle.
- GPIO13 à la masse au démarrage efface la configuration.

**EEPROM.**

| Adresse | Contenu |
|---|---|
| `0x00` – `0x1F` | SSID du poêle |
| `0x20` – `0x3F` | Mot de passe |
| `0x40` | Drapeau d'appairage (`B` = vient d'être appairé, `A` = déjà annoncé) |
| `0x41` – `0x42` | Dernier intervalle de sommeil reçu, en hexadécimal |

### 2.2 Module WiFi 1 (local), firmware 1.2.4

**Rôle.** Point d'accès WiFi du poêle et passerelle websocket vers la liaison série de la carte mère. C'est à lui que se connectent l'application en mode direct et la sonde d'ambiance.

**Réseau.**

- SSID : `MCZ-01` suivi de l'adresse MAC du point d'accès en hexadécimal majuscule.
- Mot de passe : dérivé du SSID (voir encadré).
- Adresse du module : `192.168.120.1`.
- DHCP : adresses `.100` à `.200`, 4 clients au maximum.
- Portail captif sur le port 80, qui affiche seulement « Connesso con successo alla stufa ».

> **Dérivation du mot de passe.** Le mot de passe est la concaténation des codes ASCII décimaux des caractères du SSID aux positions 15, 13, 17, 12 et 16 (en comptant à partir de 0).
> Exemple fictif : pour `MCZ-01AABBCCDDEEFF`, ces caractères sont `E`, `D`, `F`, `D`, `F`, soit `69`, `68`, `70`, `68`, `70`, donc le mot de passe `6968706870`.

**Passerelle.**

- Serveur websocket sur le port 81, sous-protocole `arduino`, 5 clients au maximum.
- Une trame texte commençant par `C` est envoyée telle quelle à la carte mère, suivie de `^` et d'un saut de ligne.
- La réponse est lue jusqu'au `^`, les `%7C` sont remplacés par `|`, puis elle est renvoyée au seul client demandeur.
- Une trame commençant par `P` sert de signe de vie, sans réponse. Tout le reste est ignoré.

**Autres comportements.**

- Au démarrage, puis après 30 secondes sans trafic, envoi à la carte mère de `RispostaAccensioneDirect|<ssid>|<mot de passe>|1.2.4^`.
- Toutes les 20 secondes, ping websocket des clients. Un client en `.51` à `.60` (plage des sondes) qui ne répond pas est déconnecté ; un client DHCP muet depuis 20 secondes provoque un redémarrage du module.
- Vitesse série : 115200 bauds par défaut. La commande `CambioBaudSerial` permet 9600, 38400 ou 57600 ; la valeur est gardée en EEPROM.
- LED : GPIO13 clignote à 1 Hz ; GPIO12 est mis à 0 au démarrage et n'est plus modifié, ce qui laisse sa LED allumée (elle est active à l'état bas).

### 2.3 Module WiFi 2 (cloud), firmware « MCZ-RemoteService » 1.2.5

**Rôle.** Accès distant. Le module se connecte au WiFi de la maison, puis à un serveur socket.io, et relaie les commandes du serveur vers la carte mère.

**Déroulement.**

1. Envoi à la carte mère de `RispostaAccensioneRemoto|<MAC>|1.2.5^`.
2. Connexion au WiFi enregistré en EEPROM. Sans réseau, ouverture d'un point d'accès ouvert « MCZ-RemoteService » avec une page de choix du réseau.
3. Demande de l'adresse du serveur à la carte mère avec `RecuperoSerialeIP^`. La réponse contient quatre champs séparés par `|` ; les trois derniers sont le numéro de série, l'hôte et le port. **L'adresse du serveur n'est donc pas dans le firmware.**
4. Connexion socket.io (chemin `/socket.io/?EIO=3`) et envoi de l'événement `join` avec le numéro de série, l'adresse MAC, le type `stove` et la révision.
5. À chaque commande reçue : le serveur envoie un objet JSON avec `richiesta`, `socketChiamata` et `idChiamata`. Le contenu de `richiesta` part vers la carte mère suivi de `^` ; la réponse est renvoyée dans un événement `rispondo`, avec les deux identifiants recopiés.

**Autres comportements.**

- Ping socket.io toutes les 25 secondes. Redémarrage si le WiFi tombe.
- Après 30 secondes sans message du serveur, nouvelle annonce série à la carte mère.
- Vitesse série : même mécanisme que le module local, mémorisée à l'adresse EEPROM `0x46`.
- LED : GPIO13 clignote pendant le portail de configuration puis suit l'état de la connexion ; GPIO12 est mis à 0 au démarrage, ce qui allume sa LED.

**EEPROM.**

| Adresse | Contenu |
|---|---|
| `0x00` – `0x1F` | SSID du WiFi de la maison |
| `0x20` – `0x3F` | Mot de passe du WiFi, en clair |
| `0x40` | Drapeau écrit par le portail de configuration |
| `0x46` – `0x49` | Vitesse série divisée par 2, en hexadécimal |

### 2.4 Limites de l'analyse

- Les noms de fonctions et de variables n'existent pas dans les binaires ; la logique a été relue dans le pseudo-C produit par Ghidra.
- Quelques points sont des interprétations plus que des lectures directes : le détail du clignotement des LED, l'effacement de la configuration WiFi au démarrage du module local, le redémarrage sur perte du WiFi du module cloud.
- Le chiffrement de la connexion au serveur distant n'a pas été vérifié.
- Le rôle du premier champ de la réponse à `RecuperoSerialeIP` est inconnu.

---

## 3. Protocole série de la carte mère

**Liaison.** UART principal de l'ESP8266 (TX sur GPIO1, RX sur GPIO3), 115200 bauds.

**Format.**

- Envoi : `<commande>^` suivi d'un retour à la ligne.
- Réponse : champs séparés par `|`, terminée par `^`. Le séparateur peut arriver encodé en `%7C`.
- Les valeurs de la trame d'information sont en hexadécimal.

### Commandes

| Commande | Effet |
|---|---|
| `C\|RecuperoInfo` | Demande la trame d'information (type `01`) |
| `C\|WriteParametri\|<n°>\|<valeur>` | Écrit un paramètre |
| `C\|SalvaDataOra\|jjmmaaaaHHMM` | Règle la date et l'heure |
| `C\|RecuperaTemperaturaWiFi\|<sonde>\|…` | Température d'une sonde WiFi, `<sonde>` valant 51, 52 ou 53 pour les sondes 1 à 3 (voir 2.1) |
| `C\|WriteBancaDati\|356\|1\|FF` | Envoyée par la sonde à son premier contact |
| `CambioBaudSerial` | Changement de vitesse série |
| `RecuperoSerialeIP` | Numéro de série et adresse du serveur distant |

### Paramètres d'écriture utilisés

| N° | Fonction | Valeurs |
|---|---|---|
| 34 | Marche / arrêt | `1` = allumer, `40` = éteindre |
| 42 | Consigne de température | température × 2 |
| 36 | Puissance | `11` à `15` pour les puissances 1 à 5 |
| 37 | Ventilation frontale | `0` = arrêt (« No Air »), 1 à 5 = vitesse manuelle, `6` = automatique |
| 38 | Ventilation canalisée 1 | `0` = arrêt (« No Air »), 1 à 5 = vitesse manuelle, `6` = automatique |
| 35 | Mode Active | 0 / 1 |
| 40 | Mode de régulation | `0` = manuel, `1` = automatique |
| 41 | Mode éco | 0 / 1 |
| 45 | Mode silencieux | 0 / 1 |
| 50 | Sons | 0 / 1 |
| 1111 | Chronothermostat | 0 / 1 |
| 1 | Acquittement d'alarme | `255` |
| 43 | Remise à zéro du compteur d'entretien | `0` |

### Champs de la trame d'information utilisés

| Champ | Contenu | Conversion |
|---|---|---|
| 1 | État du poêle | code (voir ci-dessous) |
| 2 | Ventilation frontale | 0 = arrêt (« No Air »), 1 à 5 = vitesse manuelle, 6 = automatique |
| 3 | Ventilation canalisée 1 | 0 = arrêt (« No Air »), 1 à 5 = vitesse manuelle, 6 = automatique |
| 5 | Température des fumées | °C |
| 6 | Température ambiante | ÷ 2 |
| 10 | Bougie | 0 = éteinte |
| 11 | Active, consigne | sans unité |
| 12 | Extracteur de fumées | tr/min |
| 13 | Vis sans fin, consigne | tr/min |
| 14 | Vis sans fin, réelle | tr/min |
| 17 | Brasier | 0 = propre |
| 18 | Profil | code |
| 20 | Mode Active | 0 / 1 |
| 21 | Active, mesure | sans unité |
| 22 | Mode de régulation | 0 = manuel, 1 = automatique |
| 23 | Mode éco | 0 / 1 |
| 24 | Mode silencieux | 0 / 1 |
| 25 | Chronothermostat | 0 / 1 |
| 26 | Consigne | ÷ 2 |
| 28 | Température carte mère | ÷ 2 |
| 29 | Puissance | 11 à 15 pour les puissances 1 à 5 |
| 30 | Firmware carte mère | 3 octets : `0x010802` = 1.8.2 |
| 32 – 36 | Heure, minute, jour, mois, année | |
| 37 | Durée totale de fonctionnement | secondes |
| 38 – 42 | Durée en puissance 1 à 5 | secondes |
| 43 | Heures avant entretien | heures |
| 45 | Nombre d'allumages | |
| 46 | Active, température | sans unité |
| 49 | Sons | 0 / 1 |
| 52 | Température de la sonde WiFi 1 | ÷ 2 |

### États du poêle

| Code | État | Code | État |
|---|---|---|---|
| 0 | Éteint | 31 | Allumé |
| 1 | Contrôle chaud/froid | 40 | Extinction |
| 2 | Nettoyage à froid | 41 | Refroidissement |
| 3 | Chargement pellets à froid | 42 – 43 | Nettoyage bas / haut |
| 4 – 5 | Démarrage 1 / 2 à froid | 44 | Déblocage vis |
| 6 | Nettoyage à chaud | 45 | Auto éco |
| 7 | Chargement pellets à chaud | 46 | Veille |
| 8 – 9 | Démarrage 1 / 2 à chaud | 49 | Chargement vis |
| 10 | Stabilisation | 50 – 67 | Alarmes A01 à A23 |
| 11 – 15 | Puissance 1 à 5 | 69 | Attente alarmes sécurité |
| 30, 48 | Diagnostic | | |

La table complète des champs, des commandes et des alarmes vient du projet [Chibald/maestrogateway](https://github.com/Chibald/maestrogateway).

---

## 4. Connexions pour la reprogrammation

Les deux ESP8266 sont sur la même carte. La reprogrammation utilise trois points d'accès : le connecteur du module (liaison série), un connecteur 2 broches (mode programmation) et deux vias (alimentation de l'ESP à programmer).

### Alimentation de la carte

En fonctionnement normal, les ESP8266 ne sont pas alimentés directement par le connecteur :

- le +5 V du connecteur alimente un régulateur AMS1117, qui produit le 3,3 V de la carte ;
- chaque ESP8266 reçoit ce 3,3 V à travers un transistor ;
- la carte mère pilote ces transistors par d'autres broches du connecteur, et met les deux ESP8266 sous tension l'un après l'autre au démarrage, pour éviter un fort appel de courant.

Conséquence pour la reprogrammation : **le +5 V du connecteur n'est pas utilisé**. Sans la carte mère pour piloter les transistors, il n'alimenterait aucun ESP8266. On alimente uniquement l'ESP8266 à programmer, en 3,3 V, par deux vias de la carte.

### Connecteur du module WiFi

Le connecteur est une embase à deux rangées. La broche 1 est du côté du module WiFi 2 et des 2 LED (repère `J5 WIFI2` sur la carte). Les broches impaires sont sur une rangée, les broches paires sur l'autre.

| Broche | Signal | Module | Utilisée pour la programmation |
|---|---|---|---|
| 1 | TX | Module WiFi 1 (local) | oui, pour le module 1 |
| 2 | RX | Module WiFi 1 (local) | oui, pour le module 1 |
| 3 | TX | Module WiFi 2 (cloud) | oui, pour le module 2 |
| 4 | RX | Module WiFi 2 (cloud) | oui, pour le module 2 |
| 5 | GND | | oui |
| 6 | +5 V | | non |

Les autres broches, dont celles par lesquelles la carte mère commande l'alimentation des ESP8266, ne sont pas répertoriées ici.

![Brochage du connecteur du module WiFi](images/Connecteur.png)

*Figure 1 — Connecteur du module WiFi, côté soudures et côté embase : TX et RX des deux modules, masse et position de la broche 1.*

![Vue d'ensemble de la carte WiFi](images/carte-vue-ensemble.png)

*Figure 2 — Carte WiFi, les deux faces. Côté composants : module 1 (« Local », repère `J3 WIFI1`) et module 2 (« Cloud », repère `J5 WIFI2`). Côté opposé : l'embase du connecteur vers la carte mère.*

### Vias d'alimentation

La carte comporte un via par ESP8266. Chaque via contourne le transistor et arrive directement sur la broche 3V3 de son ESP8266 : on y applique le 3,3 V pour alimenter uniquement le module à programmer.

| Via | Emplacement sur la carte |
|---|---|
| 3,3 V du module WiFi 1 (local) | au-dessus du module, près du repère `J3 WIFI1` |
| 3,3 V du module WiFi 2 (cloud) | près des condensateurs C6 et C9, à côté du repère `J5 WIFI2` |

![Vias d'alimentation 3,3 V des deux modules](images/vias-3v3-esp.png)

*Figure 3 — Vias d'alimentation 3,3 V : « WiFi Direct » pour le module 1, « WiFi Cloud » pour le module 2.*

### Connecteur 2 broches (mode programmation)

Ce connecteur se trouve entre le module WiFi 1 et le connecteur principal, près du repère `ROA`.

| Broche | Signal |
|---|---|
| Côté module WiFi 1 | GND |
| Côté connecteur principal | GPIO0 des deux ESP8266 |

Relier ces deux broches met GPIO0 à la masse pour les deux ESP8266. Seul celui qui est alimenté par son via démarre en mode programmation ; l'autre n'est pas sous tension et n'est pas touché.

![Connecteur 2 broches GND / GPIO0](images/GPIO0.png)

*Figure 4 — Connecteur 2 broches : GND et GPIO0.*

### Câblage vers l'adaptateur USB-série

Pour reprogrammer le **module WiFi 2 (cloud)** :

| Carte WiFi | Adaptateur USB-série |
|---|---|
| Broche 3 (TX module 2) | RX |
| Broche 4 (RX module 2) | TX |
| Broche 5 (GND) | GND |
| Via d'alimentation du module 2 | 3,3 V |

Pour le module WiFi 1 (local), utiliser la broche 1 (TX), la broche 2 (RX) et le via d'alimentation du module 1.

**Précautions.**

- Débrancher la carte du poêle avant de la relier à l'adaptateur.
- Les signaux TX et RX se croisent : le TX de la carte va sur le RX de l'adaptateur.
- Alimenter en 3,3 V uniquement, et régler l'adaptateur sur des niveaux logiques 3,3 V. Ne jamais appliquer de 5 V sur un via.
- Un ESP8266 consomme des pointes de plusieurs centaines de milliampères en WiFi. Si l'adaptateur ne fournit pas assez de courant en 3,3 V, la programmation échoue de façon aléatoire : utiliser alors une alimentation 3,3 V séparée, masse commune avec l'adaptateur.

---

## 5. Reprogrammation avec esptool

Remplacer `COM3` par le port de l'adaptateur (`/dev/ttyUSB0` sous Linux). Avec une version d'esptool antérieure à la 5, les commandes s'écrivent avec un tiret bas (`read_flash`, `erase_flash`, `write_flash`, `--flash_mode`, `--flash_size`).

### Entrer en mode programmation

1. Poser le pont entre GND et GPIO0.
2. Appliquer le 3,3 V sur le via de l'ESP8266 à programmer.
3. Le pont peut rester en place pendant toutes les opérations esptool. Après chaque commande, couper puis rétablir le 3,3 V pour revenir en mode programmation.

### Vérifier la connexion

```
esptool --port COM3 flash-id
```

### Sauvegarder le firmware d'origine

À faire avant toute écriture. C'est la seule façon de revenir en arrière.

```
esptool --port COM3 --baud 115200 read-flash 0 0x200000 backup-wifi2.img
```

### Effacer la flash

```
esptool --port COM3 erase-flash
```

L'effacement supprime les restes de l'ancien firmware, dont le mot de passe WiFi stocké en clair, et évite qu'ESPHome démarre sur d'anciens réglages.

### Écrire le firmware ESPHome

Sur ESP8266, ESPHome produit un seul fichier, à écrire à l'adresse `0x0`. Il ne contient que le programme (environ 470 ko), pas une image complète de la flash.

```
esptool --port COM3 --baud 115200 write-flash --flash-mode dout --flash-size 2MB 0x0 firmware.bin
```

Le mode `dout` fonctionne sur toutes les puces flash.

### Redémarrer

1. Couper le 3,3 V.
2. Retirer le pont GND–GPIO0.
3. Remettre le 3,3 V pour un essai sur table, ou remonter la carte sur le poêle : le module rejoint le WiFi et apparaît dans Home Assistant. S'il ne se connecte pas, il ouvre un point d'accès de secours.

Aucun message n'apparaît sur le port série : les logs y sont désactivés, la liaison étant réservée au poêle. Ils passent par le WiFi. Les mises à jour suivantes se font par le WiFi (OTA).

### Revenir au firmware d'origine

```
esptool --port COM3 write-flash --flash-mode dout --flash-size 2MB 0x0 backup-wifi2.img
```

---

## 6. Configuration ESPHome

Le firmware du module WiFi 2 (cloud) est remplacé par ESPHome, avec le composant externe `mcz_maestro` du dépôt (dossier `components/mcz_maestro`). Le module WiFi 1 (local) n'est pas modifié : l'application en mode direct et le point d'accès du poêle continuent de fonctionner.

Deux configurations complètes sont fournies :

- `examples/mcz-ego2-fr.yaml` : celle du poêle étudié, avec les noms d'entités en français utilisés dans ce document ;
- `examples/full.yaml` : toutes les entités disponibles.

### Principe

- Le module se connecte au WiFi de la maison et à Home Assistant par l'API native d'ESPHome.
- Il interroge la carte mère avec `C|RecuperoInfo` toutes les 15 secondes. La carte mère répond à une écriture par la trame d'information à jour ; si ce n'est pas le cas, le module la redemande une fois après l'écriture ou la série d'écritures.
- Une seule commande est en cours à la fois sur la liaison série, avec 2 secondes d'attente maximum.
- Au démarrage, il envoie l'annonce `RispostaAccensioneRemoto|<MAC>|<version>`, comme le firmware d'origine.
- On ne déclare que les entités voulues : celles qui ne sont pas listées ne sont ni compilées ni exposées dans Home Assistant.

### Déclaration du composant

```yaml
external_components:
  - source: github://lexyan/mcz-maestro-esphome
    components: [ mcz_maestro ]

logger:
  baud_rate: 0          # la liaison série est réservée au poêle

uart:
  tx_pin: GPIO1
  rx_pin: GPIO3
  baud_rate: 115200

mcz_maestro:
  id: stove
  language: fr
  time_id: ha_time
  virtual_probe:
    temperature_sensor: ha_room_temp
    probe: 1
```

| Option | Défaut | Rôle |
|---|---|---|
| `update_interval` | `15s` | Période d'interrogation de la carte mère |
| `language` | `en` | Langue des textes publiés (état, vanne, pellets) : `en` ou `fr` |
| `announce` | `true` | Annonce du module à la carte mère au démarrage |
| `module_version` | `1.2.6` | Version envoyée dans cette annonce |
| `write_guard` | `20s` | Délai après le démarrage pendant lequel aucune écriture n'est envoyée |
| `time_id` | | Source de l'heure utilisée par le bouton `set_time` |
| `virtual_probe` | | Sonde virtuelle (voir plus bas) |

Secrets attendus par les exemples, dans `secrets.yaml` : `wifi_ssid`, `wifi_password`, `esphome_encryption_key`, `ap_wifi_password`. Les mises à jour OTA sont chiffrées avec la clé de l'API.

### Matériel

| Élément | Réglage |
|---|---|
| Carte | `esp_wroom_02` (ESP8266 générique, 2 Mo) |
| Liaison série | TX GPIO1, RX GPIO3, 115200 bauds |
| Logs série | Désactivés (`baud_rate: 0`) |
| LED d'état | GPIO13, active à l'état bas. Éteinte en fonctionnement normal, clignotante en cas d'avertissement ou d'erreur |
| LED GPIO12 | Active à l'état bas : mise à 0 au démarrage, donc allumée. Non exposée dans Home Assistant |

### Entités

Chaque entité se déclare par sa clé, sous la plateforme correspondante :

```yaml
sensor:
  - platform: mcz_maestro
    ambient_temperature:
      name: "Température ambiante"
```

Le nom est libre. Les colonnes « Champ » et « Paramètre » renvoient à la partie 3.

**Thermostat (`climate`)**

Une seule entité, sans clé : arrêt / chauffage, consigne, température ambiante et deux préréglages pour le mode de régulation. Les options `manual_preset` et `auto_preset` fixent leurs libellés (« Manuel » et « Automatique » dans l'exemple). Paramètres 34, 42 et 40.

**Capteurs (`sensor`)**

| Clé | Contenu | Champ |
|---|---|---|
| `ambient_temperature` | Température ambiante | 6 |
| `fume_temperature` | Température des fumées | 5 |
| `power_level` | Puissance réelle, 1 à 5 | 29 |
| `state_code` | État du poêle (code) | 1 |
| `board_temperature` | Température de la carte mère | 28 |
| `fume_fan_rpm` | Extracteur de fumées | 12 |
| `auger_rpm`, `auger_rpm_set` | Vis sans fin, réelle et consigne | 14, 13 |
| `active_set`, `active_live`, `active_temperature` | Valeurs Active, sans unité | 11, 21, 46 |
| `profile` | Profil (code) | 18 |
| `total_hours` | Heures de fonctionnement | 37 |
| `hours_power_1` à `hours_power_5` | Heures par puissance | 38 à 42 |
| `hours_to_service` | Heures avant entretien | 43 |
| `ignitions` | Nombre d'allumages | 45 |
| `minutes_to_switch_off` | Minutes avant extinction | 44 |
| `wifi_probe_1` à `wifi_probe_3` | Sondes WiFi lues par le poêle | 52 à 54 |
| `virtual_probe_temperature` | Dernière température envoyée par la sonde virtuelle | |
| `virtual_probe_interval` | Intervalle demandé par le poêle, en minutes | |

**Capteurs binaires (`binary_sensor`)**

| Clé | Contenu | Champ |
|---|---|---|
| `alarm` | Alarme (état 50 à 67) | 1 |
| `brazier_dirty` | Brasier à nettoyer | 17 |
| `igniter` | Bougie | 10 |
| `link` | La carte mère répond sur la liaison série | |

**Textes (`text_sensor`)**

| Clé | Contenu | Champ |
|---|---|---|
| `state` | État du poêle en clair | 1 |
| `datetime` | Date et heure du poêle | 32 à 36 |
| `firmware` | Firmware de la carte mère | 30 |

**Interrupteurs (`switch`)**

| Clé | Contenu | Champ | Paramètre |
|---|---|---|---|
| `power` | Marche / arrêt | 1 | 34 |
| `eco_mode` | Mode éco | 23 | 41 |
| `silent_mode` | Mode silencieux | 24 | 45 |
| `active_mode` | Mode Active | 20 | 35 |
| `chronothermostat` | Chronothermostat | 25 | 1111 |
| `sounds` | Sons | 49 | 50 |
| `virtual_probe` | Active la sonde virtuelle | | |

**Listes (`select`)**

| Clé | Contenu | Champ | Paramètre |
|---|---|---|---|
| `control_mode` | Mode de régulation | 22 | 40 |
| `fan` | Ventilation frontale | 2 | 37 |
| `ducted_fan_1` | Ventilation canalisée 1 | 3 | 38 |

L'option `options` associe chaque valeur envoyée au poêle à un libellé, ce qui permet de les traduire :

```yaml
select:
  - platform: mcz_maestro
    control_mode:
      name: "Mode de régulation"
      options: { 0: "Manuel", 1: "Automatique" }
```

**Nombres (`number`)**

| Clé | Contenu | Champ | Paramètre |
|---|---|---|---|
| `setpoint` | Consigne, 5 à 35 °C par pas de 0,5 | 26 | 42 |
| `power` | Réglage de puissance, 1 à 5 (envoyé comme 11 à 15), refusé en mode automatique | 29 | 36 |

**Boutons (`button`)**

| Clé | Contenu | Paramètre |
|---|---|---|
| `refresh` | Relit la trame d'information et les autres trames utilisées (recettes, réglages, alarmes…) | |
| `reset_alarm` | Acquitte l'alarme | 1 = `255` |
| `set_time` | Règle l'heure du poêle (option `time_id` du composant) | |
| `reset_service` | Remet à zéro le compteur d'entretien (« heures avant entretien ») | 43 = `0` |

**Entités selon la configuration du poêle**

Ces entités concernent les poêles hydro, le ballon, la 2ᵉ canalisation et le capteur de pellets.

| Plateforme | Clé | Contenu | Champ | Paramètre |
|---|---|---|---|---|
| `select` | `ducted_fan_2` | Ventilation canalisée 2 | 4 | 39 |
| `sensor` | `puffer_temperature` | Température du ballon tampon | 7 | |
| `sensor` | `boiler_temperature` | Température du ballon sanitaire | 8 | |
| `sensor` | `ntc3_temperature` | Température de la sonde NTC3 | 9 | |
| `sensor` | `return_temperature` | Température de retour | 59 | |
| `sensor` | `pump_pwm` | Pompe, valeur brute | 16 | |
| `text_sensor` | `valve_3way` | Vanne 3 voies : Sanitaire (1) / Chauffage | 15 | |
| `number` | `boiler_setpoint` | Consigne du ballon | 27 | 51 |
| `sensor` | `pellet_sensor_code` | Capteur de pellets : 0 = absent, 10 = niveau correct, 11 = vide | 47 | |
| `text_sensor` | `pellet_level` | Niveau de pellets en clair | 47 | |
| `binary_sensor` | `pellet_empty` | Réservoir de pellets vide | 47 | |
| `switch` | `pellet_sensor` | Capteur de pellets | 47 | 148 |
| `select` | `season_mode` | Mode été / hiver : 0 = hiver, 1 = été | 51 | 58 |
| `number` | `chrono_t1` à `chrono_t3` | Températures du chronothermostat, sans relecture | | 1108 à 1110 |
| `number` | `profile` | Profil, valeur brute | 18 | 149 |
| `number` | `temperature_unit` | Unité de température, valeur brute | 48 | 49 |
| `number` | `sleep` | Sleep, valeur brute | 50 | 57 |
| `number` | `antifreeze` | Antigel, valeur brute | 60 | 154 |
| `button` | `reset_active` | Réinitialise la fonction Active | | 2 = `255` |
| `button` | `load_auger` | Charge la vis sans fin | | 34 = `49` |
| `sensor` | `modbus_address`, `database_id`, `field_51`, `field_55`, `set_puffer`, `set_boiler`, `set_health` | Valeurs brutes, sens non documenté | 19, 31, 51, 55, 56 à 58 | |

- Ces entités viennent de la table de maestrogateway et **n'ont pas été testées** : le poêle étudié n'a aucune de ces options.
- Pour les températures optionnelles, la valeur `255` est traitée comme « sonde absente ».
- Le bouton `load_auger` amène des pellets dans le brasier : à n'utiliser que poêle éteint et froid.
- Ne sont volontairement pas proposées : les commandes de diagnostic (`C|Diagnostica|…`, pilotage direct de l'extracteur, de la vis, de la bougie, des ventilateurs, de la pompe et de la vanne) et la réinitialisation d'usine (paramètre 46). Elles restent accessibles par une trame brute.

### Thermostat

L'entité thermostat regroupe la marche/arrêt, la consigne et la température ambiante dans une carte thermostat de Home Assistant.

| État du poêle | Mode affiché | Activité affichée |
|---|---|---|
| Allumage et combustion (1 à 15, 31) | Chauffage | En chauffe |
| Auto éco, veille (45, 46) | Chauffage | Inactif |
| Tous les autres (éteint, extinction, refroidissement, alarmes) | Arrêt | Arrêt |

- Passer en mode chauffage envoie le paramètre 34 à `1` ; passer en arrêt l'envoie à `40`.
- Changer la consigne envoie le paramètre 42, arrondi au demi-degré.
- L'état affiché vient uniquement de ce que le poêle renvoie. Après une commande, il se met à jour à la trame d'information suivante.
- Les deux préréglages portent le mode de régulation : en choisir un envoie le paramètre 40.
- Les entités `power`, `setpoint` et `control_mode` peuvent être déclarées en plus ; elles restent synchronisées avec le thermostat.

### Mode de régulation

Le poêle a deux modes de fonctionnement :

- **Automatique** : il module lui-même sa puissance selon la consigne et la température ambiante. C'est le thermostat qui pilote le poêle.
- **Manuel** : il fonctionne à la puissance choisie. La consigne du thermostat est sans effet ; le thermostat ne sert plus qu'à la marche et à l'arrêt.

Le mode se choisit par les préréglages du thermostat ou par la liste `control_mode` ; les deux lisent le champ 22 et écrivent le paramètre 40 (0 = manuel, 1 = automatique).

La puissance est exposée par deux entités :

| Clé | Rôle |
|---|---|
| `power_level` (`sensor`) | Lecture seule : puissance réelle, dans les deux modes |
| `power` (`number`) | Commande, utile en mode manuel. En mode automatique, l'écriture est refusée et un avertissement est écrit dans les logs |

Pour n'afficher que les commandes utiles, on peut conditionner la visibilité des cartes du tableau de bord à l'état de la liste `control_mode` : thermostat et capteur de puissance en automatique, réglage de puissance en manuel.

### Recettes air et pellets

Les recettes (« Ricetta Aria » et « Ricetta Pellet » dans l'application MCZ) corrigent le dosage de l'air de combustion et des pellets. Elles s'exposent par deux listes (`select`) :

| Clé | Contenu | Valeurs | Cellule écrite |
|---|---|---|---|
| `air_recipe` | Recette air | 0 à 4, affichées -2 à +2 | 459 |
| `pellet_recipe` | Recette pellets | 0 à 6, affichées -3 à +3 | 460 |

```yaml
select:
  - platform: mcz_maestro
    air_recipe:
      name: "Recette air"
    pellet_recipe:
      name: "Recette pellets"
```

- Lecture : `C|RecuperoParametriExtra|11`, envoyée au démarrage, après une écriture, puis toutes les 10 minutes. La réponse est une trame de type `03`, en hexadécimal : `03|<air>|<pellets>|<entrée ambiance>|<délai éco-stop>|<hystérésis>`.
- Écriture : `C|WriteBancaDati|<cellule>|1|<valeur sur 2 chiffres hexadécimaux>`. Exemple : `C|WriteBancaDati|459|1|03` règle la recette air sur +1.
- La lecture n'est envoyée que si une entité qui en dépend est déclarée.
- Une recette est écrite dans la banque de données du poêle, c'est-à-dire ses paramètres, probablement en mémoire permanente : la modifier à la main, pas depuis une automatisation répétitive.
- Les libellés se traduisent avec `options`, comme pour les autres listes.
- Dans une lambda, `write_database(cellule, octets, valeur)` écrit une cellule de la banque de données.
- Le format vient des sources de l'application MCZ ; lecture et écriture ont été vérifiées sur un Ego 2 (carte mère 1.8.2).

### Réglages et informations complémentaires

Ces entités reprennent des menus de l'application MCZ. Elles sont lues par d'autres trames que la trame d'information ; une trame n'est demandée que si une entité qui en dépend est déclarée.

**Réglages** (catégorie configuration)

| Plateforme | Clé | Contenu | Écriture |
|---|---|---|---|
| `number` | `eco_stop_delay` | Eco stop : délai avant l'arrêt, 1 à 30 minutes | cellule 148, 2 octets, en secondes |
| `number` | `eco_stop_hysteresis` | Eco stop : écart sous la consigne pour le redémarrage, 2 à 5 °C | cellule 294 |
| `select` | `room_input` | Entrée ambiance : sonde WiFi (255), thermostat (0) ou sonde du poêle (1) | cellule 356 |
| `select` | `wifi_probe_interval` | Intervalle d'envoi de la sonde WiFi : 10, 15 ou 20 minutes | paramètre 110 |
| `select` | `wifi_probe_summer_interval` | Intervalle d'envoi en mode été : 45, 60, 90 ou 120 minutes | paramètre 111 |
| `select` | `wifi_probe_offset` | Décalage de la sonde WiFi, -5 à +5 °C | paramètre 144 |

**Informations** (lecture seule, catégorie diagnostic)

| Plateforme | Clé | Contenu |
|---|---|---|
| `text_sensor` | `last_alarm` | Dernière alarme et sa date |
| `text_sensor` | `alarm_history` | Cinq dernières alarmes avec leur description et leur date, séparées par « \| » |
| `text_sensor` | `wifi_probe_last_seen` | Dernière connexion de la sonde WiFi |
| `sensor` | `wifi_probe_signal` | Signal de la sonde WiFi vu par le poêle, en % |
| `text_sensor` | `bootloader_version`, `wifi_direct_version`, `wifi_remote_version`, `wifi_probe_version` | Versions du bootloader, des modules WiFi 1 et 2 et de la sonde |
| `text_sensor` | `database_name`, `database_revision` | Banque de données chargée dans la carte mère |
| `text_sensor` | `serial_number` | Numéro de série |
| `sensor` | `setpoint_min`, `setpoint_max` | Bornes de la consigne acceptées par le poêle |
| `binary_sensor` | `fan_fitted`, `ducted_fan_1_fitted`, `ducted_fan_2_fitted`, `silent_mode_available` | Équipement du poêle |

**Trames lues**

| Contenu | Commande | Type | Lecture |
|---|---|---|---|
| Recettes, entrée ambiance, eco stop | `C\|RecuperoParametriExtra\|11` | `03` | Au démarrage, après une écriture, puis toutes les 10 minutes |
| Équipement et bornes | `C\|RecuperoParametri` | `00` | Une fois |
| Versions | `C\|RecuperoVersioneSW` | `0E` | Une fois |
| Sonde WiFi | `C\|RecuperoSondeWiFi` | `0B` | Au démarrage, après une écriture, puis toutes les 10 minutes |
| Alarmes | `C\|RecuperaAllarmi` | `0A` | Au démarrage, quand une alarme apparaît ou disparaît, puis toutes les 10 minutes |

- Tant qu'une trame n'a pas reçu de réponse, elle est redemandée chaque minute. Le bouton `refresh` relit aussi toutes ces trames, ce qui fait apparaître aussitôt un réglage changé depuis l'application.
- Choisir « Thermostat » pour l'entrée ambiance passe aussi le poêle en régulation automatique (paramètre 40), comme le fait l'application.
- La sonde virtuelle n'envoie rien tant que l'entrée ambiance n'est pas « Sonde WiFi » : sinon sa température remplacerait la mesure de la sonde du poêle (constaté sur le poêle). L'entrée ambiance est lue pour cela même si la liste `room_input` n'est pas déclarée ; revenir sur « Sonde WiFi » relance l'envoi aussitôt.
- Les dates sont celles de l'horloge du poêle.
- La trame des versions contient aussi le nom et le mot de passe du point d'accès du poêle : le composant ne les expose pas et n'écrit pas cette trame dans les logs.
- L'eco stop et l'entrée ambiance sont écrits dans la banque de données du poêle, comme les recettes : mêmes précautions.
- Les formats viennent des sources de l'application MCZ ; lectures et écritures ont été vérifiées sur un Ego 2 (carte mère 1.8.2), avec deux valeurs par liste.

### Trames brutes

Le composant expose deux fonctions utilisables dans une lambda : `send_command("…")` envoie une trame brute, sans le `^` final, et `write_parameter(n°, valeur)` écrit un paramètre. Les exemples s'en servent pour offrir une action à Home Assistant, utile pour tester une commande non prévue :

```yaml
api:
  actions:
    - action: send_command
      variables:
        command: string
      then:
        - lambda: 'id(stove).send_command(command);'
```

```yaml
action: esphome.mcz_ego2_send_command
data:
  command: "C|WriteParametri|42|43"
```

### Sonde virtuelle

Elle remplace la sonde d'ambiance déportée par un capteur de Home Assistant. Elle se configure dans le composant, avec l'option `virtual_probe`.

Les poêles à sorties canalisées peuvent avoir jusqu'à trois sondes WiFi. Dans ce cas, `virtual_probe` prend une liste, avec une entrée par numéro de sonde :

```yaml
mcz_maestro:
  id: stove
  virtual_probe:
    - temperature_sensor: temp_salon
      probe: 1
    - temperature_sensor: temp_zone_2
      probe: 2
```

Chaque sonde suit son propre intervalle d'envoi. Les capteurs `virtual_probe_temperature` et `virtual_probe_interval` concernent la sonde 1 ; ceux des sondes 2 et 3 s'appellent `virtual_probe_2_temperature`, `virtual_probe_2_interval`, `virtual_probe_3_temperature` et `virtual_probe_3_interval`. L'envoi de plusieurs sondes n'a pas été testé sur un poêle : l'association d'une sonde à une sortie canalisée se règle côté poêle.

| Option | Défaut | Rôle |
|---|---|---|
| `temperature_sensor` | | Identifiant du capteur ESPHome à envoyer, par exemple un capteur `homeassistant` |
| `probe` | `1` | Sonde simulée : 1, 2 ou 3, envoyée comme `51`, `52` ou `53` |
| `version` | `1.9.9` | Version annoncée par la sonde |
| `require_api` | `true` | N'envoie que si Home Assistant est connecté |

- Trame envoyée : `C|RecuperaTemperaturaWiFi|<sonde>|<T × 2>|<version>|00|<qualité WiFi>`.
- La température est arrondie au demi-degré le plus proche (23,3 °C est envoyé comme 23,5 °C), ce qui évite le biais vers le bas d'une troncature.
- Premier envoi 30 secondes après le démarrage, puis à l'intervalle renvoyé par le poêle, borné entre 1 et 30 minutes.
- Aucun envoi si le capteur est indisponible ou si Home Assistant est injoignable.
- L'interrupteur `virtual_probe`, s'il est déclaré, permet de suspendre les envois de toutes les sondes ; il est actif à chaque démarrage.
- Éteindre la sonde d'origine, sinon les deux envoient chacune leur température.
- Si la sonde virtuelle cesse d'envoyer des températures valides, le poêle repasse automatiquement en mode manuel et l'application affiche « sonde wifi déconnectée ».

### Sécurités

- **Aucune commande au démarrage.** Le composant n'écrit rien tant qu'une entité n'est pas actionnée ; tous les états viennent de ce que le poêle renvoie. Une première version de la configuration utilisait des interrupteurs ESPHome génériques, qui se remettent par défaut à « éteint » au démarrage et exécutent leur action d'extinction : le module envoyait six écritures à chaque démarrage, ce qui a mis le poêle en route lors d'un essai.
- **Garde-fou.** Toute écriture est ignorée pendant les 20 premières secondes après le démarrage (option `write_guard`).
- **Puissance en mode automatique.** Le réglage de puissance est refusé tant que le poêle est en mode automatique.
- **Pas d'écriture en flash.** Dans les exemples, les préférences ne sont jamais écrites en flash (`flash_write_interval: never`).


---

## Sources

- [Chibald/maestrogateway](https://github.com/Chibald/maestrogateway) : table des commandes, des champs de la trame d'information et des états du poêle.
- Dumps des trois firmwares (`backup.img`, `backup-wifi1.img`, `backup-wifi2.img`), analysés avec Ghidra.
