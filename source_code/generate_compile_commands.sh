#!/bin/sh
make --always-make --dry-run | grep -wE 'cc' | grep -w '\-c' | jq -nR '[inputs|{directory:"/root/swordfish/source_code", command:., file: match(" [^ ]+$").string[1:]}]'  > compile_commands.json
