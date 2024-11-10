# mash (maneesh's shell)

# Building & Running

- Run `make -f Makefile.[Linux|Darwin]`
  - On Windows, run `nmake -f Makefile.Windows`
- Run `./mash`
  - Can add flags such as `-v` for verbose mode

# Implements

- Process Creation (Running sub-programs) (ls -la)
- Built-in Shell programs (cd, exit)
- Piping (ls -la | more)
- Variable Substitution (var=hello) (echo ${var})
- Background Processes (sleep 5 &)
- File Redirection (ls -la > files.txt)
- Filename Globbing on UNIX (ls \*.c)
