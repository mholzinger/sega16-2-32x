! Fold 1's hole map (tools/bake_cat1hole.py, LOOP29 244): two bits per
! tilemap cell of every scene, 0 no hole / 1 suppress the whole cell /
! 2 consult the tile art per pixel. 5 scenes x 5120 bytes.
        .section .rodata
        .align  4
        .global _cat1hole
_cat1hole:
        .incbin "cat1hole.bin"
