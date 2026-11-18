set pagination off
set architecture arm
target remote host.docker.internal:2159
info threads
info registers pc sp lr
