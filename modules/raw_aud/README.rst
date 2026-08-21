RAW audio
=========

EOS 200D firmware 1.0.1 RAW audio capture for MLV Lite.

The module uses the DIGIC 7 Aproc PCM producer at 48 kHz, signed 16-bit stereo,
pre-arms capture during the MLV recorder lifecycle, and writes standard WAVI
and AUDF blocks through MLV Lite metadata slots. Audio timestamps use the same
MLV time origin as video.

This module is camera-specific and intentionally does not alter RAW/EDMAC/
PACK32 geometry, SD clocks, or voltage state. Direct Aproc stop remains
unproven, so the current safety guard allows one recording per power cycle.

:Summary: EOS 200D RAW audio capture for MLV Lite.
:Author: Jok3r28
:License: GPL
