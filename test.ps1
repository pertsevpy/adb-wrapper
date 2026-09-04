$ErrorActionPreference = "Stop"

# Конфигурация
$wrapperDir = "."
$adbReal = Join-Path $wrapperDir "adb.real.exe"
$fastbootReal = Join-Path $wrapperDir "fastboot.real.exe"
$adbWrapper = Join-Path $wrapperDir "adb.exe"
$fastbootWrapper = Join-Path $wrapperDir "fastboot.exe"
$logFile = Join-Path $wrapperDir "adb_trace.log"

# Проверяем, что реальные файлы существуют
if (-not (Test-Path $adbReal)) {
    Write-Error "adb.real.exe not found"
    exit 1
}
if (-not (Test-Path $fastbootReal)) {
    Write-Error "fastboot.real.exe not found"
    exit 1
}

# Убедимся, что врапперы есть
if (-not (Test-Path $adbWrapper)) {
    Write-Error "adb.exe (wrapper) not found"
    exit 1
}
if (-not (Test-Path $fastbootWrapper)) {
    Write-Error "fastboot.exe (wrapper) not found"
    exit 1
}

# Функция для очистки лога перед тестом
function Clear-Log {
    if (Test-Path $logFile) {
        Remove-Item $logFile -Force
    }
}

# Функция для проверки лога
function Test-LogContains {
    param([string]$pattern)
    if (-not (Test-Path $logFile)) {
        Write-Error "Log file not found"
        return $false
    }
    $content = Get-Content $logFile -Raw
    if ($content -match $pattern) {
        return $true
    }
    Write-Error "Log does not contain pattern: $pattern"
    return $false
}

# Тест 1: adb --version
Write-Host "Test 1: adb --version"
Clear-Log
$output = & $adbWrapper --version 2>&1
if ($LASTEXITCODE -ne 0) {
    Write-Error "adb --version failed with exit code $LASTEXITCODE"
    exit 1
}
# Проверяем, что в выводе есть "Android Debug Bridge"
if ($output -notmatch "Android Debug Bridge") {
    Write-Error "Output does not contain 'Android Debug Bridge'"
    exit 1
}
# Проверяем лог
if (-not (Test-LogContains "START.*TYPE=adb")) {
    exit 1
}
if (-not (Test-LogContains "EXIT=0")) {
    exit 1
}
Write-Host "Test 1 passed"

# Тест 2: fastboot --version
Write-Host "Test 2: fastboot --version"
Clear-Log
$output = & $fastbootWrapper --version 2>&1
if ($LASTEXITCODE -ne 0) {
    Write-Error "fastboot --version failed with exit code $LASTEXITCODE"
    exit 1
}
if ($output -notmatch "fastboot version") {
    Write-Error "Output does not contain 'fastboot version'"
    exit 1
}
if (-not (Test-LogContains "START.*TYPE=fastboot")) {
    exit 1
}
if (-not (Test-LogContains "EXIT=0")) {
    exit 1
}
Write-Host "Test 2 passed"

# Тест 3: Неизвестная команда (ожидаем ненулевой код)
Write-Host "Test 3: adb unknown-command"
Clear-Log
& $adbWrapper unknown-command 2>&1 | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Error "adb unknown-command returned 0, expected non-zero"
    exit 1
}
if (-not (Test-LogContains "EXIT=")) {
    # Проверяем, что код завершения залогирован (может быть не 0)
    Write-Error "Log does not contain EXIT line"
    exit 1
}
Write-Host "Test 3 passed"

# Тест 4: Запуск без консоли (имитация GUI)
Write-Host "Test 4: Run without console (hidden window)"
Clear-Log
# Запускаем враппер как скрытый процесс, перенаправляя вывод в файл
$tempOut = Join-Path $env:TEMP "adb_output.txt"
$process = Start-Process -FilePath $adbWrapper -ArgumentList "--version" -WindowStyle Hidden -RedirectStandardOutput $tempOut -PassThru -Wait
if ($process.ExitCode -ne 0) {
    Write-Error "Hidden process failed with exit code $($process.ExitCode)"
    exit 1
}
$content = Get-Content $tempOut -Raw
if ($content -notmatch "Android Debug Bridge") {
    Write-Error "Output from hidden process does not contain expected text"
    exit 1
}
Remove-Item $tempOut -Force
if (-not (Test-LogContains "START.*TYPE=adb")) {
    exit 1
}
Write-Host "Test 4 passed"

Write-Host "All tests passed"