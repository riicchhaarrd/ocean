./build && ./ocean $1 bin/example.elf && bin/example.elf

printf "\nExit code: %d\n" $?
