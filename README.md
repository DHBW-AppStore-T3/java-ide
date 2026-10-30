# Java-IDE

Browser-IDE (code-server) mit **JDK 21** und **Maven** für Java-Übungen. Jede Person bekommt einen eigenen Pod mit
eigenem Link, eigenem Passwort und eigenem Speicher (5 GiB). Studierende müssen nichts installieren.

Standard ist ein Pod je Person (`scope: user`). Mit `scopeChoice: true` kann die Lehrperson im Wizard unter
„Experteneinstellungen“ stattdessen einen Pod je Gruppe wählen; die Mitglieder einer Gruppe teilen sich dann
Pod, Speicher und Passwort.

## Was enthalten ist

| Bestandteil | Version |
|---|---|
| code-server | 4.144.0 |
| OpenJDK (`javac`, `java`) | 21 (Debian-Paket) |
| Maven (`mvn`) | Debian-Paket |
| Java-Extensions | `redhat.java` 1.58.0, `vscjava.vscode-java-debug` 0.60.0 |

Im Terminal der IDE laufen `javac`, `java` und `mvn`. `~/.m2` und alle Dateien liegen im persistenten Speicher unter
`/home/coder` und bleiben beim erneuten Öffnen erhalten. Internetzugang (nur TCP 80/443) ist für Maven Central
eingeschaltet.

Die Umgebungen eines Deployments erreichen sich gegenseitig (`podNetwork`, startet **an**), damit Studierende eigene TCP- und UDP-Server und -Clients schreiben und Nachrichten austauschen können: jeder TCP- und UDP-Port, unter dem Namen der Umgebung (`<workload>`) und alle zusammen unter `peers` (DNS liefert die Adressen aller). Die Lehrperson kann beides im Wizard abschalten.

### UDP-Broadcast

Ein echtes Layer-2-Broadcast gibt es im Cluster nicht (`255.255.255.255` oder Multicast würde die Pods aller Deployments erreichen und wird verworfen). Damit Übungsprogramme trotzdem wie gewohnt per Broadcast senden können, ist im Image eine kleine Bibliothek vorgeladen (`LD_PRELOAD`, Quelltext in `image/peerbroadcast/peerbroadcast.c`): Sendet ein Programm ein UDP-Datagramm an eine Broadcast- oder Multicast-Adresse, geht dasselbe Datagramm stattdessen an jede Adresse von `peers`, vom selben Socket und an denselben Port. Das Programm und die Empfänger brauchen dafür keine Änderung: der Empfänger bindet den Port wie bei einem echten Broadcast.

```java
var socket = new DatagramSocket();
socket.setBroadcast(true);
byte[] data = "Hallo an alle".getBytes();
socket.send(new DatagramPacket(data, data.length, InetAddress.getByName("255.255.255.255"), 4711));
```

- Als Broadcast gelten `255.255.255.255`, `224.0.0.0/4`, die Broadcast-Adresse des eigenen IPv4-Subnetzes (z. B. `10.0.0.255`), `ff00::/8` (auch `ff02::1`) und deren IPv4-Formen in `::ffff:0:0/96`.
- Der Absender bekommt sein eigenes Datagramm ebenfalls, wie bei einem echten Broadcast (`peers` enthält ihn).
- Jede Umgebung erhält das Datagramm genau einmal, auch wenn `peers` IPv4- und IPv6-Adressen liefert.
- Das Datagramm erreicht nur die Umgebungen desselben Deployments: `peers` kennt keine anderen.
- Nicht abgedeckt: `send`/`write` auf einem Socket, der mit `connect` an eine Broadcast-Adresse gebunden wurde, `sendmmsg`, statisch gelinkte Programme und Programme, die nicht die C-Bibliothek nutzen (z. B. Go). Dort gilt weiter: an jede Adresse von `peers` senden.
- Ist `podNetwork` ausgeschaltet, löst `peers` nicht auf; ein Broadcast geht dann unverändert hinaus (und kommt nirgends an).
- Abschalten im Terminal: `APPSTORE_PEER_BROADCAST=off java ...`. Mit `APPSTORE_PEERS_NAME` lässt sich ein anderer Name als `peers` verwenden.

## Build und Version

Das Image (`image/Dockerfile`) baut `.github/workflows/ci.yml` mit der zentralen Pipeline aus `app-registry` (Vertrag,
hadolint, Smoke-Test, Trivy, Push nach ghcr.io). Nach einer Änderung an `image/`:

1. Digest aus der Job-Summary in `appstore.yaml` eintragen (`ghcr.io/dhbw-appstore-t3/apps/java-ide@sha256:...`).
2. Version taggen (`v1.0.0`) und im AppStore einreichen.

Siehe die Schritt-für-Schritt-Anleitung im README von `app-registry`.
