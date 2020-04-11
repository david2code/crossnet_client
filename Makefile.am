
bin_PROGRAMS=crossnet_client

crossnet_client_SOURCES=backend.c\
backend.h\
buff.c\
buff.h\
config.h\
hash_table.h\
heaptimer.c\
heaptimer.h\
kernel_list.h\
log.c\
log.h\
main.c\
main.h\
misc.c\
misc.h\
notify.c\
notify.h\
unique_id.c\
unique_id.h

crossnet_client_LDADD=-L/lib64/mysql -L/usr/local/lib -lpthread -lz -lcrypto -ljson-c -lcurl
