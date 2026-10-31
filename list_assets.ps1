$res = Invoke-RestMethod -Uri 'https://api.github.com/repos/ggml-org/llama.cpp/releases/tags/b12014'
$res.assets | Select-Object name, size
