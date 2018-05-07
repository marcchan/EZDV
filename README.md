## RealTime Programming Practice
das EchtzeitDatenVerarbeitungsPraktikum in Hochschule Emden Leer 

## Allgemeine Beschreibung

Im Praktikum Echtzeitdatenverarbeitung soll das Modell einer Fertigungsanlage (Raum E13)
auf Basis eines Linux Real-Time Kernels (RTAI) automatisiert werden. Dabei erfolgt die
Programmierung in der Sprache ANSI C99 auf Betriebssystem-Kernel-Ebene (Linux
Kernelmodul Programmierung) . Mindestens ein Teilmodell (Bearbeiten, Wareneingang
bzw. Ausgang., Transportwareneingang bzw. Ausgang, Lager und Distributionszentrum
Eingang bzw. Ausgang) ist hierbei von den Studenten in Gruppenarbeit ( Zweiergruppen ) zu
wählen und anschließend selbstständig zu automatisieren. Außerhalb des hier gesteckten
Rahmens ist es den Studierenden dabei freigestellt, ob zusätzliche Funktionen oder Teile
anderer Modelle (mit)implementiert werden. Der hierfür verfügbare Zeitrahmen kann der
nachfolgenden Tabelle entnommen werden. Die Bearbeitung ist dabei in Meilensteine
eingeteilt, die einzuhalten sind. Wurde ein Meilenstein nicht erfolgreich erreicht, gilt das
gesamte Praktikum als nicht bestanden .

## zu verwendende Kurzbezeichnungen sind:
* WA (Warenausgang)
* WE (Wareneingang)
* DZA (Distributionszentrum-Ausgang)
* DZE (Distributionszentrum-Eingang)
* L (Lager)
* TWE (Transportwareneingang)
* TWA (Transportwarenausgang)
* B (Bearbeiten)

## Thema:Bearbeiten / machining
### Hinweiß:
* Parallele Ausführung der Arbeitsschritte Prüfen, Spannen, Entgraten, Auswerfen in
Abhängigkeit des Vorhandenseins eines Bauteils im entsprechenden Zustand
(Schlechtteil, Gutteil).
* Die Bauteile werden beim Funktionstest manuell geliefert und je nach Zustand
(Schlechtteil, Gutteil) bearbeitet und im Anschluss ausgeworfen.
