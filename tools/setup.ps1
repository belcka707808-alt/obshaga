# ОБЩАГА — установка всего нужного на Windows одним запуском.
# Запускать через setup.bat в корне репозитория. Скрипт можно запускать повторно:
# всё, что уже установлено, он пропускает.

$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$UProject = Join-Path $ProjectRoot 'Obshaga.uproject'

function Write-Step([string]$Text) { Write-Host ''; Write-Host "==> $Text" -ForegroundColor Cyan }
function Write-Ok([string]$Text) { Write-Host "    OK: $Text" -ForegroundColor Green }
function Write-Note([string]$Text) { Write-Host "    $Text" -ForegroundColor Yellow }

function Test-Command([string]$Name) {
    return [bool](Get-Command $Name -ErrorAction SilentlyContinue)
}

function Update-SessionPath {
    $machine = [Environment]::GetEnvironmentVariable('Path', 'Machine')
    $user = [Environment]::GetEnvironmentVariable('Path', 'User')
    $env:Path = "$machine;$user"
}

function Install-WingetPackage([string]$Id, [string[]]$ExtraArgs = @()) {
    if (-not (Test-Command 'winget')) {
        throw 'Не найден winget. Установите «Установщик приложений» (App Installer) из Microsoft Store и запустите setup.bat ещё раз.'
    }
    $wingetArgs = @('install', '--id', $Id, '--exact', '--accept-source-agreements', '--accept-package-agreements') + $ExtraArgs
    & winget @wingetArgs
    Update-SessionPath
}

function Get-VisualStudioWithCpp {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { return $null }
    $path = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($path) { return $path }
    return $null
}

function Get-AnyVisualStudio {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { return $null }
    $path = & $vswhere -latest -products * -property installationPath
    if ($path) { return $path }
    return $null
}

function Get-UnrealEngines {
    $found = @()

    $registryKey = 'HKLM:\SOFTWARE\EpicGames\Unreal Engine'
    if (Test-Path $registryKey) {
        foreach ($key in Get-ChildItem $registryKey) {
            $dir = (Get-ItemProperty $key.PSPath -ErrorAction SilentlyContinue).InstalledDirectory
            if ($dir) { $found += [pscustomobject]@{ Version = $key.PSChildName; Dir = $dir } }
        }
    }

    $launcherData = Join-Path $env:ProgramData 'Epic\UnrealEngineLauncher\LauncherInstalled.dat'
    if (Test-Path $launcherData) {
        try {
            $data = Get-Content $launcherData -Raw | ConvertFrom-Json
            foreach ($item in $data.InstallationList) {
                if ($item.AppName -match '^UE_(\d+\.\d+)$') {
                    $found += [pscustomobject]@{ Version = $Matches[1]; Dir = $item.InstallLocation }
                }
            }
        }
        catch { }
    }

    $defaultRoot = Join-Path $env:ProgramFiles 'Epic Games'
    if (Test-Path $defaultRoot) {
        foreach ($dir in Get-ChildItem $defaultRoot -Directory -Filter 'UE_5.*') {
            $found += [pscustomobject]@{ Version = $dir.Name.Substring(3); Dir = $dir.FullName }
        }
    }

    return @($found |
        Where-Object { $_.Version -match '^5\.\d+$' -and (Test-Path (Join-Path $_.Dir 'Engine\Binaries\Win64\UnrealEditor.exe')) } |
        Sort-Object -Property Dir -Unique |
        Sort-Object -Property { [version]$_.Version } -Descending)
}

function Get-EpicLauncher {
    foreach ($base in @(${env:ProgramFiles(x86)}, $env:ProgramFiles)) {
        foreach ($arch in @('Win64', 'Win32')) {
            $exe = Join-Path $base "Epic Games\Launcher\Portal\Binaries\$arch\EpicGamesLauncher.exe"
            if (Test-Path $exe) { return $exe }
        }
    }
    return $null
}

try {
    Write-Host 'ОБЩАГА — НЕ СПАЛИМСЯ: установка' -ForegroundColor Cyan
    Write-Host "Папка проекта: $ProjectRoot"
    if ($ProjectRoot -match '[^\x00-\x7F]') {
        Write-Note 'В пути к проекту есть русские буквы или другие не-латинские символы.'
        Write-Note 'Unreal Engine с такими путями иногда ломается. Лучше перенести проект, например в C:\Projects\obshaga.'
    }

    # 1. Git и Git LFS (LFS входит в Git for Windows)
    Write-Step 'Git и Git LFS'
    if (-not (Test-Command 'git')) { Install-WingetPackage 'Git.Git' }
    if (-not (Test-Command 'git')) { throw 'Git не установился. Перезапустите компьютер и запустите setup.bat ещё раз.' }
    git lfs install | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Git LFS не настроился. Переустановите Git for Windows и запустите setup.bat ещё раз.' }
    Write-Ok (git --version)

    # 2. Claude Code
    Write-Step 'Claude Code'
    $claudeBin = Join-Path $env:USERPROFILE '.local\bin'
    if (Test-Path $claudeBin) { $env:Path = "$env:Path;$claudeBin" }
    if (-not (Test-Command 'claude')) {
        powershell -NoProfile -ExecutionPolicy Bypass -Command "[Net.ServicePointManager]::SecurityProtocol = 'Tls12'; irm https://claude.ai/install.ps1 | iex"
        Update-SessionPath
        if (Test-Path $claudeBin) { $env:Path = "$env:Path;$claudeBin" }
    }
    if (Test-Command 'claude') { Write-Ok 'Claude Code установлен' }
    else { Write-Note 'Claude Code не найден. Можно поставить вручную: https://claude.com/download' }

    # 3. Visual Studio 2022 с C++ (компилятор для кода игры)
    Write-Step 'Visual Studio 2022 с C++'
    $workloads = @(
        '--add', 'Microsoft.VisualStudio.Workload.NativeDesktop',
        '--add', 'Microsoft.VisualStudio.Workload.NativeGame',
        '--add', 'Microsoft.VisualStudio.Workload.ManagedDesktop',
        '--includeRecommended'
    )
    if (-not (Get-VisualStudioWithCpp)) {
        $existingVs = Get-AnyVisualStudio
        if ($existingVs) {
            Write-Note 'Visual Studio есть, но без C++. Добавляю нужные компоненты (20-60 минут). На запрос Windows нажмите «Да».'
            $installer = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\setup.exe'
            $modifyArgs = @('modify', '--installPath', "`"$existingVs`"", '--passive', '--norestart') + $workloads
            Start-Process -FilePath $installer -ArgumentList $modifyArgs -Wait -Verb RunAs
        }
        else {
            Write-Note 'Ставлю Visual Studio 2022 Community (20-60 минут). На запрос Windows нажмите «Да».'
            $override = (@('--passive', '--wait', '--norestart') + $workloads) -join ' '
            Install-WingetPackage 'Microsoft.VisualStudio.2022.Community' @('--override', $override)
        }
    }
    if (-not (Get-VisualStudioWithCpp)) {
        throw 'C++ для Visual Studio пока не установлен. Дождитесь, пока закроется окно установщика Visual Studio, и запустите setup.bat ещё раз.'
    }
    Write-Ok 'Visual Studio с C++ найдена'

    # 4. Unreal Engine
    Write-Step 'Unreal Engine 5'
    $engines = @(Get-UnrealEngines)
    if ($engines.Count -eq 0) {
        if (-not (Get-EpicLauncher)) { Install-WingetPackage 'EpicGames.EpicGamesLauncher' }
        $launcher = Get-EpicLauncher
        if ($launcher) { Start-Process $launcher }
        Write-Host ''
        Write-Note 'Unreal Engine ещё не установлен. Это единственный шаг, который нужно сделать руками:'
        Write-Note '  1. В Epic Games Launcher войдите в аккаунт Epic (или зарегистрируйтесь).'
        Write-Note '  2. Слева «Unreal Engine» -> вкладка «Library» -> жёлтый «+» -> «Install».'
        Write-Note '  3. Дождитесь конца установки (это долго, нужно около 60 ГБ).'
        Write-Note '  4. Запустите setup.bat ещё раз — дальше всё сделается само.'
        return
    }
    $engine = $engines[0]
    Write-Ok "Unreal Engine $($engine.Version): $($engine.Dir)"

    # 5. Привязать проект к установленной версии движка
    Write-Step 'Настройка проекта'
    $projectText = Get-Content $UProject -Raw -Encoding UTF8
    $newText = $projectText -replace '"EngineAssociation":\s*"[^"]*"', "`"EngineAssociation`": `"$($engine.Version)`""
    if ($newText -ne $projectText) {
        [IO.File]::WriteAllText($UProject, $newText, (New-Object Text.UTF8Encoding $false))
    }
    Write-Ok "Obshaga.uproject привязан к UE $($engine.Version)"

    # MCP-сервер редактора стартует сам — через него Claude управляет редактором
    $iniDir = Join-Path $ProjectRoot 'Saved\Config\WindowsEditor'
    $ini = Join-Path $iniDir 'EditorPerProjectUserSettings.ini'
    $mcpSection = '[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]'
    New-Item -ItemType Directory -Force -Path $iniDir | Out-Null
    if (-not ((Test-Path $ini) -and (Select-String -Path $ini -SimpleMatch $mcpSection -Quiet))) {
        Add-Content -Path $ini -Encoding ASCII -Value @('', $mcpSection, 'bAutoStartServer=True')
    }
    Write-Ok 'Автозапуск MCP-сервера в редакторе включён'

    # 6. Сборка C++-кода игры
    Write-Step 'Сборка проекта (первый раз — несколько минут)'
    $buildBat = Join-Path $engine.Dir 'Engine\Build\BatchFiles\Build.bat'
    & $buildBat ObshagaEditor Win64 Development $UProject -WaitMutex
    if ($LASTEXITCODE -ne 0) {
        throw "Сборка не удалась (код $LASTEXITCODE). Скопируйте текст ошибки выше и отправьте его Claude."
    }
    Write-Ok 'Проект собран'

    # 7. Открыть редактор
    Write-Step 'Открываю Unreal Editor'
    $editor = Join-Path $engine.Dir 'Engine\Binaries\Win64\UnrealEditor.exe'
    Start-Process -FilePath $editor -ArgumentList "`"$UProject`""

    Write-Host ''
    Write-Host 'Готово!' -ForegroundColor Green
    Write-Host 'Дальше: откройте PowerShell в папке проекта, наберите claude и напишите «Продолжаем M0».'
    Write-Host 'Claude спросит, можно ли подключить плагин Unreal и сервер unreal-mcp, — нажмите «Да».'
}
catch {
    Write-Host ''
    Write-Host "ОШИБКА: $($_.Exception.Message)" -ForegroundColor Red
    Write-Host 'Скопируйте этот текст и отправьте Claude — разберёмся.'
}
