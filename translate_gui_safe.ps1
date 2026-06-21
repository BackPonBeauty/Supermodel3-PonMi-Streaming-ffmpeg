git checkout Src/OSD/SDL/Gui.cpp

# Load translations into a hash table
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

# Read Gui.cpp as lines in Shift-JIS
$sjis = [System.Text.Encoding]::GetEncoding(932)
$guiLines = [System.IO.File]::ReadAllLines("Src/OSD/SDL/Gui.cpp", $sjis)

$newLines = [System.Collections.Generic.List[string]]::new()
foreach ($line in $guiLines) {
    $trimmedLine = $line.Trim()
    $replaced = $false
    
    # Try exact match first
    if ($translations.ContainsKey($trimmedLine)) {
        $leadingWhitespace = $line.Substring(0, $line.Length - $trimmedLine.Length)
        $newLines.Add($leadingWhitespace + $translations[$trimmedLine])
        $replaced = $true
    } else {
        # Try finding if any key in translation is part of the line
        foreach ($key in $translations.Keys) {
            if ($trimmedLine.Contains($key)) {
                $newLine = $line.Replace($key, $translations[$key])
                $newLines.Add($newLine)
                $replaced = $true
                break
            }
        }
    }
    
    if (-not $replaced) {
        # If it still contains Japanese, let's clean up the comment part
        if ($line -match '^(?<prefix>.*?)(?<comment>//.*)$') {
            $prefix = $Matches['prefix']
            $comment = $Matches['comment']
            if ($comment -match '[\u3040-\u30ff\u4e00-\u9faf\uff00-\uffef]') {
                # Strip Japanese characters
                $comment = $comment -replace '[\u3040-\u30ff\u4e00-\u9faf\uff00-\uffef]', ''
                $newLines.Add($prefix + $comment)
                $replaced = $true
            }
        }
    }
    
    if (-not $replaced) {
        $newLines.Add($line)
    }
}

[System.IO.File]::WriteAllLines("Src/OSD/SDL/Gui.cpp", $newLines, [System.Text.Encoding]::UTF8)
Write-Output "Translation process finished."
