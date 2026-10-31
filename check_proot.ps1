$res = Invoke-RestMethod -Uri 'https://api.github.com/repos/proot-me/proot/releases/latest'
$res.assets | Select-Object name, browser_download_url
