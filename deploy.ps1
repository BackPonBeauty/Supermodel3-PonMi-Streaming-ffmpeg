$src = "H:\Supermodel3-PonMi-main5\bin64\supermodel.exe"
$srcHash = (Get-FileHash -Path $src -Algorithm MD5).Hash
Write-Host "Source: $src ($srcHash)"

for ($i = 1; $i -le 8; $i++) {
    $dir = "{0:D2}" -f $i
    $dst = "C:\Users\vf2_c\Desktop\8p\$dir\Supermodel.exe"
    Copy-Item -Path $src -Destination $dst -Force
    $h = (Get-FileHash -Path $dst -Algorithm MD5).Hash
    Write-Host "$dst : $h"
}
