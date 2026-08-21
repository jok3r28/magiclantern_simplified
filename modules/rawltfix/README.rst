RAWLTFIX
========

EOS 200D firmware 1.0.1 guarded 10/12-bit RAW EDMAC pitch correction.

The module only filters the known Canon per-frame channel-23 RAW size request
and changes xb from the stock 14-bit pitch to the already-active 12-bit or
10-bit raw_info pitch when all expected descriptor and geometry guards match.

:Summary: Guarded EOS 200D 10/12-bit RAW EDMAC pitch correction.
:Author: Jok3r28
:License: GPL
