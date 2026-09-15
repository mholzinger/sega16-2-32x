ssh root@mister.office.local 'echo "core: $(cat /tmp/CORENAME 2>/dev/null || basename "$(cat /tmp/STARTPATH 2>/dev/null)")"; echo "game: $(cat /var/log/ACTIVEGAME)"'                                                                                          

