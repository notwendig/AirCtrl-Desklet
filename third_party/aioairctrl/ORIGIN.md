C++-Port aus aioairctrl-cpp, Version 0.1.0, vom 2026-09-01.
Python-Original: https://github.com/betaboon/aioairctrl
Commit: c97640b054c14c0d02739fdfa2564cd85f8216ea
MIT-Lizenz, Copyright 2020 betaboon.
`encryption.cpp` bleibt unverändert. Der lokale C++-Transport in `coap.cpp` und
`client.cpp` ergänzt einen bindbaren Quellport, CoAP-Keepalives, eine begrenzte
Observe-Neuanmeldung und eine kurze Abmeldefrist. Diese Härtungen ändern weder
Verschlüsselungsformat noch Gerätestatus. Der Benutzer hat die Statusabfrage mit
diesem Code am Philips AC2729/10 getestet.
