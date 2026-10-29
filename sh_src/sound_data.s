! The game's sound blobs. Cart 0x260000 (sh_src/mars.ld .sounddata), the MD tile blob's old
! room (NOTILESMD=1). R60SOUND: _snd_blob = tools/sound_blob.py over the sound lab's rendered
! music + per-command tracks, streamed to the Z80 by the 68K (md_src/md_sound.c) through the
! banked 0x900000 window. R60SPEECH: _speech_blob = tools/speech_blob.py over the lab's speech
! bank at 8 kHz IMA ADPCM, played by the slave's PWM mixer (sh_src/r60/r60_speech.c).
        .section .sounddata,"a"
        .ifdef R60_SOUND
        .global _snd_blob
_snd_blob:
        .incbin "sh_src/sound.bin"
        .endif
        .ifdef R60_SPEECH
        .align 4
        .global _speech_blob
_speech_blob:
        .incbin "sh_src/speech.bin"
        .endif
        .ifdef R60_SFX
        .section .sfxdata,"a"
        .global _sfx_blob
_sfx_blob:
        .incbin "sh_src/sfx.bin"
        .endif
