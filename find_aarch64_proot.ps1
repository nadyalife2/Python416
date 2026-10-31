$res = Invoke-RestMethod -Uri 'https://api.github.com/repos/proot-me/proot/releases'
foreach ($rel in $res) {
    foreach ($a in $rel.assets) {
        if ($a.name -match 'aarch64|arm64') {
            Write-Host "$($rel.tag_name): $($a.name) -> $($a.browser_download_url)"
        }
    }
}
