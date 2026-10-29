# Java-IDE

Browser-IDE (code-server) mit **JDK 21** und **Maven** für Java-Übungen. Jede Person bekommt einen eigenen Pod mit
eigenem Link, eigenem Passwort und eigenem Speicher (5 GiB). Studierende müssen nichts installieren.

## Was enthalten ist

| Bestandteil | Version |
|---|---|
| code-server | 4.143.1 |
| OpenJDK (`javac`, `java`) | 21 (Debian-Paket) |
| Maven (`mvn`) | Debian-Paket |
| Java-Extensions | `redhat.java` 1.58.0, `vscjava.vscode-java-debug` 0.60.0 |

Im Terminal der IDE laufen `javac`, `java` und `mvn`. `~/.m2` und alle Dateien liegen im persistenten Speicher unter
`/home/coder` und bleiben beim erneuten Öffnen erhalten. Internetzugang (nur TCP 80/443) ist für Maven Central
eingeschaltet.

Die Umgebungen eines Deployments erreichen sich gegenseitig (`podNetwork`, startet **an**), damit Studierende eigene TCP- und UDP-Server und -Clients schreiben und Nachrichten austauschen können: jeder TCP- und UDP-Port, unter dem Namen der Umgebung (`<workload>`) und alle zusammen unter `peers` (DNS liefert die Adressen aller; ein "Broadcast" ist ein Senden an jede). Echtes `255.255.255.255` oder Multicast gibt es nicht (es würde die Pods aller Deployments erreichen). Die Lehrperson kann beides im Wizard abschalten.

## Build und Version

Das Image (`image/Dockerfile`) baut `.github/workflows/ci.yml` mit der zentralen Pipeline aus `app-registry` (Vertrag,
hadolint, Smoke-Test, Trivy, Push nach ghcr.io). Nach einer Änderung an `image/`:

1. Digest aus der Job-Summary in `appstore.yaml` eintragen (`ghcr.io/dhbw-appstore-t3/apps/java-ide@sha256:...`).
2. Version taggen (`v1.0.0`) und im AppStore einreichen.

Siehe die Schritt-für-Schritt-Anleitung im README von `app-registry`.
