# Interactive DoveOS Linux shell; the window keeps its classic appearance.
printf 'DoveOS Terminal\nLinux shell - type help for commands.\n\n'
printf '\033[4 q'
PS1='\w \$ '
export PS1
alias dir='ls -la'
alias cls='clear'
alias help='printf "Commands: dir, cls, cd, pwd, ls, cat, exit\nUse the toolbar Help button for more.\n"'
