Get-ChildItem 'C:\vcpkg\installed\x86-windows\lib' |
    Where-Object { $_.Name -match 'lame|mp3|opus|fdk|aac' } |
    Select-Object Name
