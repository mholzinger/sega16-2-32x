cd ~/src/sega16-2-32x
S=/private/tmp/claude-501/-Users-mikeholzinger-src-sega16-2-32x/c5f81f8e-7de6-48d9-b1fa-a2f1a0e38c12/scratchpad; D=/private/tmp/claude-501/-Users-mikeholzinger-src-sega16-2-32x/c5f81f8e-7de6-48d9-b1fa-a2f1a0e38c12/scratchpad/rd1dump
while pgrep -f "make line" >/dev/null; do sleep 5; done
touch sh_src/m_main.c sh_src/port_l2.c sh_src/s_main.c
make line NOSPRBAKE=1 PORTFULL=1 PORTMAIN=1 PORTOS=1 NOGAMEHIGH=1 PORTVIDEO=1 PORTPLAY=1 PORTBOOT=1 PORTCORO=1 > $D/build_rd119.log 2>&1; echo "exit=$?" >> $D/build_rd119.log
cp rom/s16.elf rom/rd119.elf; tools/save_build.sh rd119 >> $D/build_rd119.log 2>&1
make line > $D/build_line61.log 2>&1; echo "line exit=$?" >> $D/build_line41.log
echo "done119" >> $D/build_rd119.log
