pkill -9 lock_tester
pkill -9 lock_server
make
./lock_server 3772 &
./lock_tester 3772
rm yfs1 yfs2 -rf
