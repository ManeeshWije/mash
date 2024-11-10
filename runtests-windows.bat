@echo off
rem Simple test harness for "mash" shell that tests Windows-specific functionality

setlocal

rem Loop through each test file
for %%f in (tests-windows\test*.cmd) do (
    echo Testing %%f

    rem Run mash and save output
    mash.exe < %%f > %%f.output

    rem Compare output to the expected file and print differences
    echo Comparing %%f.expected with %%f.output
    fc %%f.expected %%f.output
    if errorlevel 1 (
        echo Difference found in %%f
    ) else (
        echo No differences found for %%f
    )
)

endlocal

