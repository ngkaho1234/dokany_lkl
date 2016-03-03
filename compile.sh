gcc -g -Iinclude -L. -Llkl -D_UNICODE -municode mirror.c utils.c -llkl -lws2_32 dokan1.lib dokannp1.lib  -o mirror.exe
