#!/bin/sh
make --always-make --dry-run | grep -wE 'cc' | grep -w '\-c' | jq -nR --arg dir "$(pwd)" '[inputs|{directory:$dir, command:., file: match(" [^ ]+$").string[1:]}]'  > compile_commands.json
