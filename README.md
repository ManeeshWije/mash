# mash (maneesh's shell)

Very tiny subset of bash that works on Linux, MacOS, and yes, Windows

Implements the following:

- Process Creation (Running sub-programs) (ls -la)
- Built-in Shell programs (cd, exit)
- Piping (ls -la | more)
- Variable Substitution (var=hello) (echo ${var})
- Background Processes (sleep 5 &)
- File Redirection (ls -la > files.txt)
- Filename Globbing on UNIX (ls \*.c)
