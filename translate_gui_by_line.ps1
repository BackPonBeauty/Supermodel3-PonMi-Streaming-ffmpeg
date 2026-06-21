# First, revert any local changes to Gui.cpp to get the original file
git checkout Src/OSD/SDL/Gui.cpp

# Load translations from translations.txt
$translations = @{}
$lines = Get-Content -Path "translations.txt" -Encoding utf8
foreach ($line in $lines) {
    if ($line.Trim() -eq "" -or $line.StartsWith("#")) { continue }
    $parts = $line -split '\|\|\|'
    if ($parts.Length -eq 2) {
        $jpKey = $parts[0].Trim()
        $enVal = $parts[1].Trim()
        $translations[$jpKey] = $enVal
    }
}

# Add hardcoded fallback translations by LineNumber (to avoid Japanese in script file)
$fallbackByLine = @{
    22 = "#include <ctime>   // for time, localtime, strftime"
    23 = "#include <fstream> // for ofstream"
    33 = "#define WIN32_LEAN_AND_MEAN // Constant to prevent conflicts eeeeeekkkkk"
    36 = "#include <shlobj.h>  // Required to prevent errors with BROWSEINFO"
    37 = "#include <objbase.h> // for CoTaskMemFree"
    39 = "#include <stdio.h> // for Linux popen"
    501 = "    ImGui::SameLine(0, 0); // Align horizontally with zero gap"
    1913 = "    // --- End ---"
}

# Load jp_lines.csv to map LineNumber to clean Japanese line
$csv = Import-Csv -Path "jp_lines.csv"
$lineMap = @{}
foreach ($row in $csv) {
    $lineMap[[int]$row.LineNumber] = $row.Line
}

# Read original Gui.cpp
$guiLines = [System.IO.File]::ReadAllLines("Src/OSD/SDL/Gui.cpp")

$newLines = [System.Collections.Generic.List[string]]::new()
for ($i = 0; $i -lt $guiLines.Length; $i++) {
    $lineNum = $i + 1
    $originalLine = $guiLines[$i]
    
    if ($fallbackByLine.ContainsKey($lineNum)) {
        $newLines.Add($fallbackByLine[$lineNum])
    } elseif ($lineMap.ContainsKey($lineNum)) {
        $cleanJp = $lineMap[$lineNum]
        $replaced = $false
        
        $cleanJpTrim = $cleanJp.Trim()
        if ($translations.ContainsKey($cleanJpTrim)) {
            $leadingWhitespace = $cleanJp.Substring(0, $cleanJp.Length - $cleanJpTrim.Length)
            $newLines.Add($leadingWhitespace + $translations[$cleanJpTrim])
            $replaced = $true
        } else {
            # Try matching substring in translations
            foreach ($key in $translations.Keys) {
                if ($cleanJpTrim.Contains($key)) {
                    $newLine = $cleanJp.Replace($key, $translations[$key])
                    $newLines.Add($newLine)
                    $replaced = $true
                    break
                }
            }
        }
        
        if (-not $replaced) {
            # Strip Japanese characters
            $cleanComment = $cleanJp -replace '[\u3040-\u30ff\u4e00-\u9faf\uff00-\uffef]', ''
            $newLines.Add($cleanComment)
            Write-Host "Manual clean for line $lineNum : $cleanComment"
        }
    } else {
        $newLines.Add($originalLine)
    }
}

# Write out to Gui.cpp in UTF-8
[System.IO.File]::WriteAllLines("Src/OSD/SDL/Gui.cpp", $newLines, [System.Text.Encoding]::UTF8)
Write-Output "Translation complete!"
