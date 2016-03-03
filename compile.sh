gcc -g -Iinclude -L. -Llkl -D_UNICODE -municode dokany_lkl.c utils.c -llkl -lws2_32 dokan1.lib dokannp1.lib  -o dokany_lkl.exe
