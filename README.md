LCLS2 ACM Receiver
==================

See [ACM Protocol](proto.md) specification doc.

Run [simulator](devsim.py) with `python3.7 devsim.py -B 127.0.0.1 127.0.0.1:50006`.

Test [Wireshark protocol disector](acm.lua) with `wireshark -X lua_script:acm.lua regdata.pcapng.gz`.
