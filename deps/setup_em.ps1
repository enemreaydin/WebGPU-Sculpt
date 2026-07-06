Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass

$pythonDir =  "C:\ProgramData\anaconda3\"
$env:Path = "$pythonDir;$pythonDir\Scripts;$env:Path"

& "$PSScriptRoot\emsdk\emsdk_env.ps1"