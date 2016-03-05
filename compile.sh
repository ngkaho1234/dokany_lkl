#!/bin/sh
${CC:=gcc} -g -Iinclude -Iinclude/lkl -L. -D_UNICODE -municode dokany-lkl.c utils.c -llkl -lws2_32 dokan1.lib dokannp1.lib  -o dokany-lkl.exe
